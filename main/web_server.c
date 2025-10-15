#include "web_server.h"
#include "sip_client.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "WEB_SERVER";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static esp_err_t get_sip_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Get registration status
    bool is_registered = sip_is_registered();
    cJSON_AddStringToObject(root, "status", is_registered ? "Registered" : "Not Registered");

    // Add current state
    sip_state_t state = sip_client_get_state();
    cJSON_AddNumberToObject(root, "state_code", state);

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_sip_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Get SIP configuration values, handling null returns
    const char* uri = sip_get_uri();
    const char* server = sip_get_server();
    const char* username = sip_get_username();
    const char* password = sip_get_password();

    cJSON_AddStringToObject(root, "uri", uri ? uri : "");
    cJSON_AddStringToObject(root, "server", server ? server : "");
    cJSON_AddStringToObject(root, "username", username ? username : "");
    cJSON_AddStringToObject(root, "password", password ? password : "");

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_test_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Test the SIP configuration
    bool test_result = sip_test_configuration();

    cJSON_AddStringToObject(root, "status", test_result ? "success" : "failed");
    cJSON_AddStringToObject(root, "message", test_result ?
        "SIP configuration test passed" :
        "SIP configuration test failed");

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_config_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(buf) - 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *uri = cJSON_GetObjectItem(root, "uri");
    const cJSON *server = cJSON_GetObjectItem(root, "server");
    const cJSON *username = cJSON_GetObjectItem(root, "username");
    const cJSON *password = cJSON_GetObjectItem(root, "password");

    if (cJSON_IsString(uri) && (uri->valuestring != NULL)) {
        sip_set_uri(uri->valuestring);
    }
    if (cJSON_IsString(server) && (server->valuestring != NULL)) {
        sip_set_server(server->valuestring);
    }
    if (cJSON_IsString(username) && (username->valuestring != NULL)) {
        sip_set_username(username->valuestring);
    }
    if (cJSON_IsString(password) && (password->valuestring != NULL)) {
        sip_set_password(password->valuestring);
    }

    cJSON_Delete(root);

    // Save the updated configuration to NVS
    sip_save_config(sip_get_server(), sip_get_username(), sip_get_password(),
                   sip_get_uri(), "", 5060); // Using default port

    sip_reinit(); // Re-initialize SIP client with new settings

    httpd_resp_send(req, "{\"status\":\"success\"}", strlen("{\"status\":\"success\"}"));
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static const httpd_uri_t sip_status_uri = {
    .uri       = "/api/sip/status",
    .method    = HTTP_GET,
    .handler   = get_sip_status_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t sip_config_get_uri = {
    .uri       = "/api/sip/config",
    .method    = HTTP_GET,
    .handler   = get_sip_config_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t sip_config_post_uri = {
    .uri       = "/api/sip/config",
    .method    = HTTP_POST,
    .handler   = post_sip_config_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t sip_test_uri = {
    .uri       = "/api/sip/test",
    .method    = HTTP_POST,
    .handler   = post_sip_test_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
};

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &sip_status_uri);
        httpd_register_uri_handler(server, &sip_config_get_uri);
        httpd_register_uri_handler(server, &sip_config_post_uri);
        httpd_register_uri_handler(server, &sip_test_uri);
        ESP_LOGI(TAG, "Web server started");
    } else {
        ESP_LOGE(TAG, "Error starting web server!");
    }
}
