#include "web_server.h"
#include "sip_client.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

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

static esp_err_t get_sip_log_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    // Parse query parameter "since"
    char query[64];
    uint64_t since_timestamp = 0;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(query, "since", param, sizeof(param)) == ESP_OK) {
            since_timestamp = strtoull(param, NULL, 10);  // Use strtoull for uint64_t
        }
    }
    
    // Get log entries
    sip_log_entry_t entries[50];
    int count = sip_get_log_entries(entries, 50, since_timestamp);
    
    cJSON *root = cJSON_CreateObject();
    cJSON *entries_array = cJSON_CreateArray();
    
    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        // Use double for timestamp to preserve precision in JSON
        cJSON_AddNumberToObject(entry, "timestamp", (double)entries[i].timestamp);
        cJSON_AddStringToObject(entry, "type", entries[i].type);
        cJSON_AddStringToObject(entry, "message", entries[i].message);
        cJSON_AddItemToArray(entries_array, entry);
    }
    
    cJSON_AddItemToObject(root, "entries", entries_array);
    cJSON_AddNumberToObject(root, "count", count);
    
    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_connect_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool result = sip_connect();

    cJSON_AddStringToObject(root, "status", result ? "success" : "failed");
    cJSON_AddStringToObject(root, "message", result ?
        "SIP connection initiated" :
        "SIP connection failed - check configuration");

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_disconnect_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    sip_disconnect();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", "SIP disconnected");

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

// WiFi API Handlers
static esp_err_t get_wifi_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool is_connected = wifi_is_connected();
    const char* status = is_connected ? "Connected" : "Disconnected";
    
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddBoolToObject(root, "connected", is_connected);

    if (is_connected) {
        // Get IP address and other connection info
        wifi_connection_info_t info = wifi_get_connection_info();
        cJSON_AddStringToObject(root, "ssid", info.ssid);
        cJSON_AddStringToObject(root, "ip_address", info.ip_address);
        cJSON_AddNumberToObject(root, "rssi", info.rssi);
    }

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_scan_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON *networks_array = cJSON_CreateArray();

    ESP_LOGI(TAG, "Starting WiFi scan");
    
    // Trigger WiFi scan
    wifi_scan_result_t* scan_results = NULL;
    int network_count = wifi_scan_networks(&scan_results);

    if (network_count > 0 && scan_results != NULL) {
        for (int i = 0; i < network_count; i++) {
            cJSON *network = cJSON_CreateObject();
            cJSON_AddStringToObject(network, "ssid", scan_results[i].ssid);
            cJSON_AddNumberToObject(network, "rssi", scan_results[i].rssi);
            cJSON_AddBoolToObject(network, "secure", scan_results[i].secure);
            cJSON_AddItemToArray(networks_array, network);
        }
        free(scan_results);
    }

    cJSON_AddItemToObject(root, "networks", networks_array);
    cJSON_AddNumberToObject(root, "count", network_count);

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_connect_handler(httpd_req_t *req)
{
    char buf[512];
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

    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(ssid) || (ssid->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    const char* pwd = (cJSON_IsString(password) && password->valuestring) ? password->valuestring : "";

    ESP_LOGI(TAG, "WiFi connect request: SSID=%s", ssid->valuestring);
    
    // Save WiFi configuration
    wifi_save_config(ssid->valuestring, pwd);
    
    // Attempt connection
    wifi_connect_sta(ssid->valuestring, pwd);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"WiFi connection initiated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"WiFi connection initiated\"}"));
    return ESP_OK;
}

// System API Handlers
static esp_err_t get_system_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // System uptime
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t uptime_seconds = uptime_ms / 1000;
    uint32_t hours = uptime_seconds / 3600;
    uint32_t minutes = (uptime_seconds % 3600) / 60;
    uint32_t seconds = uptime_seconds % 60;
    
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%02ld:%02ld:%02ld", hours, minutes, seconds);
    cJSON_AddStringToObject(root, "uptime", uptime_str);

    // Free heap memory
    uint32_t free_heap = esp_get_free_heap_size();
    char heap_str[32];
    snprintf(heap_str, sizeof(heap_str), "%ld KB", free_heap / 1024);
    cJSON_AddStringToObject(root, "free_heap", heap_str);

    // IP address
    wifi_connection_info_t wifi_info = wifi_get_connection_info();
    cJSON_AddStringToObject(root, "ip_address", wifi_info.ip_address);

    // Firmware version
    cJSON_AddStringToObject(root, "firmware_version", "v1.0.0");

    // Additional system info
    cJSON_AddNumberToObject(root, "free_heap_bytes", free_heap);
    cJSON_AddNumberToObject(root, "uptime_ms", uptime_ms);

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_system_restart_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "System restart requested");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"System restart initiated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"System restart initiated\"}"));

    // Restart after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

// NTP API Handlers
static esp_err_t get_ntp_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool is_synced = ntp_is_synced();
    cJSON_AddBoolToObject(root, "synced", is_synced);
    
    if (is_synced) {
        char time_str[64];
        ntp_get_time_string(time_str, sizeof(time_str));
        cJSON_AddStringToObject(root, "current_time", time_str);
        cJSON_AddNumberToObject(root, "timestamp_ms", ntp_get_timestamp_ms());
    }
    
    cJSON_AddStringToObject(root, "server", ntp_get_server());
    cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_ntp_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "server", ntp_get_server());
    cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

    const char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free((void *)json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_ntp_config_handler(httpd_req_t *req)
{
    char buf[512];
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

    const cJSON *server = cJSON_GetObjectItem(root, "server");
    const cJSON *timezone = cJSON_GetObjectItem(root, "timezone");

    if (!cJSON_IsString(server) || (server->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing server");
        return ESP_FAIL;
    }

    const char* tz = (cJSON_IsString(timezone) && timezone->valuestring) ? 
                     timezone->valuestring : NTP_DEFAULT_TIMEZONE;

    ESP_LOGI(TAG, "NTP config update: server=%s, timezone=%s", server->valuestring, tz);
    
    // Update NTP configuration
    ntp_set_config(server->valuestring, tz);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"NTP configuration updated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"NTP configuration updated\"}"));
    return ESP_OK;
}

static esp_err_t post_ntp_sync_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Manual NTP sync requested");
    
    ntp_force_sync();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"NTP sync initiated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"NTP sync initiated\"}"));
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

static const httpd_uri_t sip_log_uri = {
    .uri       = "/api/sip/log",
    .method    = HTTP_GET,
    .handler   = get_sip_log_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t sip_connect_uri = {
    .uri       = "/api/sip/connect",
    .method    = HTTP_POST,
    .handler   = post_sip_connect_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t sip_disconnect_uri = {
    .uri       = "/api/sip/disconnect",
    .method    = HTTP_POST,
    .handler   = post_sip_disconnect_handler,
    .user_ctx  = NULL
};

// URI handlers
static const httpd_uri_t root_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL
};

static const httpd_uri_t wifi_status_uri = {
    .uri = "/api/wifi/status", .method = HTTP_GET, .handler = get_wifi_status_handler, .user_ctx = NULL
};

static const httpd_uri_t wifi_scan_uri = {
    .uri = "/api/wifi/scan", .method = HTTP_POST, .handler = post_wifi_scan_handler, .user_ctx = NULL
};

static const httpd_uri_t wifi_connect_uri = {
    .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = post_wifi_connect_handler, .user_ctx = NULL
};

static const httpd_uri_t system_status_uri = {
    .uri = "/api/system/status", .method = HTTP_GET, .handler = get_system_status_handler, .user_ctx = NULL
};

static const httpd_uri_t system_restart_uri = {
    .uri = "/api/system/restart", .method = HTTP_POST, .handler = post_system_restart_handler, .user_ctx = NULL
};

static const httpd_uri_t ntp_status_uri = {
    .uri = "/api/ntp/status", .method = HTTP_GET, .handler = get_ntp_status_handler, .user_ctx = NULL
};

static const httpd_uri_t ntp_config_get_uri = {
    .uri = "/api/ntp/config", .method = HTTP_GET, .handler = get_ntp_config_handler, .user_ctx = NULL
};

static const httpd_uri_t ntp_config_post_uri = {
    .uri = "/api/ntp/config", .method = HTTP_POST, .handler = post_ntp_config_handler, .user_ctx = NULL
};

static const httpd_uri_t ntp_sync_uri = {
    .uri = "/api/ntp/sync", .method = HTTP_POST, .handler = post_ntp_sync_handler, .user_ctx = NULL
};

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20; // Increase handler limit for NTP endpoints

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register all URI handlers
        httpd_register_uri_handler(server, &root_uri);
        
        // SIP API endpoints
        httpd_register_uri_handler(server, &sip_status_uri);
        httpd_register_uri_handler(server, &sip_config_get_uri);
        httpd_register_uri_handler(server, &sip_config_post_uri);
        httpd_register_uri_handler(server, &sip_test_uri);
        httpd_register_uri_handler(server, &sip_log_uri);
        httpd_register_uri_handler(server, &sip_connect_uri);
        httpd_register_uri_handler(server, &sip_disconnect_uri);
        
        // WiFi API endpoints
        httpd_register_uri_handler(server, &wifi_status_uri);
        httpd_register_uri_handler(server, &wifi_scan_uri);
        httpd_register_uri_handler(server, &wifi_connect_uri);
        
        // System API endpoints
        httpd_register_uri_handler(server, &system_status_uri);
        httpd_register_uri_handler(server, &system_restart_uri);
        
        // NTP API endpoints
        httpd_register_uri_handler(server, &ntp_status_uri);
        httpd_register_uri_handler(server, &ntp_config_get_uri);
        httpd_register_uri_handler(server, &ntp_config_post_uri);
        httpd_register_uri_handler(server, &ntp_sync_uri);
        
        ESP_LOGI(TAG, "Web server started with all API endpoints");
    } else {
        ESP_LOGE(TAG, "Error starting web server!");
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
