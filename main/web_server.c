#include "web_server.h"
#include "sip_client.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "dtmf_decoder.h"
#include "hardware_test.h"
#include "cert_manager.h"
#include "auth_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;
static httpd_handle_t redirect_server = NULL;

// Email configuration structure
typedef struct {
    char smtp_server[64];
    uint16_t smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char recipient_email[64];
    bool enabled;
    bool configured;
} email_config_t;

// Email configuration NVS functions
static void email_save_config(const email_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("email_config", NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "smtp_server", config->smtp_server);
        nvs_set_u16(nvs_handle, "smtp_port", config->smtp_port);
        nvs_set_str(nvs_handle, "smtp_user", config->smtp_username);
        nvs_set_str(nvs_handle, "smtp_pass", config->smtp_password);
        nvs_set_str(nvs_handle, "recipient", config->recipient_email);
        nvs_set_u8(nvs_handle, "enabled", config->enabled ? 1 : 0);
        
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        
        ESP_LOGI(TAG, "Email configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for email config: %s", esp_err_to_name(err));
    }
}

static email_config_t email_load_config(void)
{
    email_config_t config = {0};
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("email_config", NVS_READONLY, &nvs_handle);
    
    if (err == ESP_OK) {
        size_t len;
        
        len = sizeof(config.smtp_server);
        if (nvs_get_str(nvs_handle, "smtp_server", config.smtp_server, &len) != ESP_OK) {
            config.smtp_server[0] = '\0';
        }
        
        if (nvs_get_u16(nvs_handle, "smtp_port", &config.smtp_port) != ESP_OK) {
            config.smtp_port = 587; // Default SMTP port
        }
        
        len = sizeof(config.smtp_username);
        if (nvs_get_str(nvs_handle, "smtp_user", config.smtp_username, &len) != ESP_OK) {
            config.smtp_username[0] = '\0';
        }
        
        len = sizeof(config.smtp_password);
        if (nvs_get_str(nvs_handle, "smtp_pass", config.smtp_password, &len) != ESP_OK) {
            config.smtp_password[0] = '\0';
        }
        
        len = sizeof(config.recipient_email);
        if (nvs_get_str(nvs_handle, "recipient", config.recipient_email, &len) != ESP_OK) {
            config.recipient_email[0] = '\0';
        }
        
        uint8_t enabled;
        if (nvs_get_u8(nvs_handle, "enabled", &enabled) == ESP_OK) {
            config.enabled = (enabled != 0);
        } else {
            config.enabled = false;
        }
        
        config.configured = (config.smtp_server[0] != '\0');
        
        nvs_close(nvs_handle);
    } else {
        config.configured = false;
        config.enabled = false;
        config.smtp_port = 587;
    }
    
    return config;
}

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t documentation_html_start[] asm("_binary_documentation_html_start");
extern const uint8_t documentation_html_end[] asm("_binary_documentation_html_end");
extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");

/**
 * @brief Check if a URI is a public endpoint that doesn't require authentication
 * 
 * @param uri The URI to check
 * @return true if the endpoint is public
 * @return false if the endpoint requires authentication
 */
static bool is_public_endpoint(const char* uri) {
    if (!uri) {
        return false;
    }
    
    // Public endpoints that don't require authentication
    const char* public_endpoints[] = {
        "/api/auth/login",           // Login endpoint
        "/api/auth/set-password",    // Initial password setup
        "/login.html",               // Login page
        "/favicon.ico",              // Browser favicon requests
        NULL
    };
    
    for (int i = 0; public_endpoints[i] != NULL; i++) {
        if (strcmp(uri, public_endpoints[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Authentication filter for HTTP requests
 * 
 * This filter checks for a valid session cookie on all protected endpoints.
 * If the session is missing or invalid, it returns 401 Unauthorized.
 * If the session is valid, it extends the session timeout.
 * 
 * @param req HTTP request
 * @return ESP_OK if authentication passed, ESP_FAIL if authentication failed
 */
static esp_err_t auth_filter(httpd_req_t *req) {
    // Skip authentication for public endpoints
    if (is_public_endpoint(req->uri)) {
        return ESP_OK;
    }
    
    // Check if password is set - if not, allow access for initial setup
    if (!auth_is_password_set()) {
        ESP_LOGW(TAG, "No password set - allowing access for initial setup");
        return ESP_OK;
    }
    
    // Extract session cookie
    char session_id[AUTH_SESSION_ID_SIZE] = {0};
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    
    if (cookie_len > 0) {
        char* cookie_str = malloc(cookie_len + 1);
        if (cookie_str) {
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_str, cookie_len + 1) == ESP_OK) {
                // Parse session_id from cookie string
                // Cookie format: "session_id=<value>; other=value"
                char* session_start = strstr(cookie_str, "session_id=");
                if (session_start) {
                    session_start += strlen("session_id=");
                    char* session_end = strchr(session_start, ';');
                    size_t session_len = session_end ? (size_t)(session_end - session_start) : strlen(session_start);
                    
                    if (session_len < AUTH_SESSION_ID_SIZE) {
                        strncpy(session_id, session_start, session_len);
                        session_id[session_len] = '\0';
                    }
                }
            }
            free(cookie_str);
        }
    }
    
    // Check if session ID was found
    if (session_id[0] == '\0') {
        ESP_LOGW(TAG, "No session cookie found for %s", req->uri);
        
        // Check if this is an API request or HTML page request
        if (strncmp(req->uri, "/api/", 5) == 0) {
            // API request - return JSON error
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Authentication required\"}", -1);
        } else {
            // HTML page request - redirect to login page
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/login.html");
            httpd_resp_send(req, NULL, 0);
        }
        return ESP_FAIL;
    }
    
    // Validate session
    if (!auth_validate_session(session_id)) {
        ESP_LOGW(TAG, "Invalid or expired session for %s", req->uri);
        
        // Check if this is an API request or HTML page request
        if (strncmp(req->uri, "/api/", 5) == 0) {
            // API request - return JSON error
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Session expired\"}", -1);
        } else {
            // HTML page request - redirect to login page
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/login.html");
            httpd_resp_send(req, NULL, 0);
        }
        return ESP_FAIL;
    }
    
    // Session is valid - extend timeout on activity
    auth_extend_session(session_id);
    
    return ESP_OK;
}

static esp_err_t get_sip_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    
    // Use sip_get_status which returns complete status including state name
    char status_buffer[512];
    sip_get_status(status_buffer, sizeof(status_buffer));
    
    httpd_resp_send(req, status_buffer, strlen(status_buffer));
    return ESP_OK;
}

static esp_err_t get_sip_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Get SIP configuration values, handling null returns
    const char* target1 = sip_get_target1();
    const char* target2 = sip_get_target2();
    const char* sip_server = sip_get_server();
    const char* username = sip_get_username();
    const char* password = sip_get_password();

    cJSON_AddStringToObject(root, "target1", target1 ? target1 : "");
    cJSON_AddStringToObject(root, "target2", target2 ? target2 : "");
    cJSON_AddStringToObject(root, "server", sip_server ? sip_server : "");
    cJSON_AddStringToObject(root, "username", username ? username : "");
    cJSON_AddStringToObject(root, "password", password ? password : "");

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_test_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Test the SIP configuration
    bool test_result = sip_test_configuration();

    cJSON_AddStringToObject(root, "status", test_result ? "success" : "failed");
    cJSON_AddStringToObject(root, "message", test_result ?
        "SIP configuration test passed" :
        "SIP configuration test failed");

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_test_call_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[256];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *target = cJSON_GetObjectItem(root, "target");
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    if (!cJSON_IsString(target) || (target->valuestring == NULL) || strlen(target->valuestring) == 0) {
        cJSON_AddStringToObject(response, "status", "failed");
        cJSON_AddStringToObject(response, "message", "Invalid or missing target");
    } else {
        // Check if registered before making call
        if (!sip_is_registered()) {
            cJSON_AddStringToObject(response, "status", "failed");
            cJSON_AddStringToObject(response, "message", "Not registered to SIP server. Please connect first.");
        } else {
            // Initiate test call
            ESP_LOGI(TAG, "Test call initiated to: %s", target->valuestring);
            sip_client_make_call(target->valuestring);
            
            cJSON_AddStringToObject(response, "status", "success");
            cJSON_AddStringToObject(response, "message", "Test call initiated");
        }
    }

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_sip_log_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
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
    
    // Allocate log entries on heap to avoid stack overflow (50 entries * 256 bytes = 12KB+)
    const int max_entries = 50;
    sip_log_entry_t *entries = malloc(max_entries * sizeof(sip_log_entry_t));
    if (!entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    int count = sip_get_log_entries(entries, max_entries, since_timestamp);
    
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
    
    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    free(entries);  // Free heap-allocated entries
    return ESP_OK;
}

static esp_err_t post_sip_connect_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool result = sip_connect();

    cJSON_AddStringToObject(root, "status", result ? "success" : "failed");
    cJSON_AddStringToObject(root, "message", result ?
        "SIP connection initiated" :
        "SIP connection failed - check configuration");

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_disconnect_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    
    sip_disconnect();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", "SIP disconnected");

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_sip_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[1024];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *target1 = cJSON_GetObjectItem(root, "target1");
    const cJSON *target2 = cJSON_GetObjectItem(root, "target2");
    const cJSON *sip_server = cJSON_GetObjectItem(root, "server");
    const cJSON *username = cJSON_GetObjectItem(root, "username");
    const cJSON *password = cJSON_GetObjectItem(root, "password");

    if (cJSON_IsString(target1) && (target1->valuestring != NULL)) {
        sip_set_target1(target1->valuestring);
    }
    if (cJSON_IsString(target2) && (target2->valuestring != NULL)) {
        sip_set_target2(target2->valuestring);
    }
    if (cJSON_IsString(sip_server) && (sip_server->valuestring != NULL)) {
        sip_set_server(sip_server->valuestring);
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
                   sip_get_target1(), sip_get_target2(), 5060); // Using default port

    sip_reinit(); // Re-initialize SIP client with new settings

    httpd_resp_send(req, "{\"status\":\"success\"}", strlen("{\"status\":\"success\"}"));
    return ESP_OK;
}

// WiFi API Handlers
static esp_err_t get_wifi_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Load WiFi configuration
    wifi_manager_config_t config = wifi_load_config();
    
    cJSON_AddStringToObject(root, "ssid", config.configured ? config.ssid : "");
    cJSON_AddBoolToObject(root, "configured", config.configured);

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    ESP_LOGI(TAG, "WiFi config save request: SSID=%s", ssid->valuestring);
    
    // Save WiFi configuration (does not connect)
    wifi_save_config(ssid->valuestring, pwd);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}"));
    return ESP_OK;
}

static esp_err_t get_wifi_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_scan_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_connect_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

// Network API Handlers
static esp_err_t get_network_ip_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool is_connected = wifi_is_connected();
    cJSON_AddBoolToObject(root, "connected", is_connected);

    if (is_connected) {
        // Get network interface
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        
        if (netif != NULL) {
            // Get IP information
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                char ip_str[16];
                char netmask_str[16];
                char gateway_str[16];
                
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                snprintf(netmask_str, sizeof(netmask_str), IPSTR, IP2STR(&ip_info.netmask));
                snprintf(gateway_str, sizeof(gateway_str), IPSTR, IP2STR(&ip_info.gw));
                
                cJSON_AddStringToObject(root, "ip_address", ip_str);
                cJSON_AddStringToObject(root, "netmask", netmask_str);
                cJSON_AddStringToObject(root, "gateway", gateway_str);
            }
            
            // Get DNS information
            esp_netif_dns_info_t dns_info;
            if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
                char dns_str[16];
                snprintf(dns_str, sizeof(dns_str), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
                cJSON_AddStringToObject(root, "dns", dns_str);
            } else {
                cJSON_AddStringToObject(root, "dns", "");
            }
        } else {
            cJSON_AddStringToObject(root, "ip_address", "");
            cJSON_AddStringToObject(root, "netmask", "");
            cJSON_AddStringToObject(root, "gateway", "");
            cJSON_AddStringToObject(root, "dns", "");
        }
    } else {
        cJSON_AddStringToObject(root, "ip_address", "");
        cJSON_AddStringToObject(root, "netmask", "");
        cJSON_AddStringToObject(root, "gateway", "");
        cJSON_AddStringToObject(root, "dns", "");
    }

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

// Email API Handlers
static esp_err_t get_email_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    email_config_t config = email_load_config();
    
    cJSON_AddStringToObject(root, "smtp_server", config.smtp_server);
    cJSON_AddNumberToObject(root, "smtp_port", config.smtp_port);
    cJSON_AddStringToObject(root, "smtp_username", config.smtp_username);
    // Do not include password in response
    cJSON_AddStringToObject(root, "recipient_email", config.recipient_email);
    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddBoolToObject(root, "configured", config.configured);

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_email_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[1024];
    int ret;
    int remaining = req->content_len;

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

    email_config_t config = email_load_config(); // Start with existing config

    const cJSON *smtp_server = cJSON_GetObjectItem(root, "smtp_server");
    const cJSON *smtp_port = cJSON_GetObjectItem(root, "smtp_port");
    const cJSON *smtp_username = cJSON_GetObjectItem(root, "smtp_username");
    const cJSON *smtp_password = cJSON_GetObjectItem(root, "smtp_password");
    const cJSON *recipient_email = cJSON_GetObjectItem(root, "recipient_email");
    const cJSON *enabled = cJSON_GetObjectItem(root, "enabled");

    // Validate and update SMTP server
    if (cJSON_IsString(smtp_server) && smtp_server->valuestring != NULL) {
        if (strlen(smtp_server->valuestring) >= sizeof(config.smtp_server)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SMTP server too long");
            return ESP_FAIL;
        }
        strncpy(config.smtp_server, smtp_server->valuestring, sizeof(config.smtp_server) - 1);
        config.smtp_server[sizeof(config.smtp_server) - 1] = '\0';
    }

    // Validate and update SMTP port
    if (cJSON_IsNumber(smtp_port)) {
        int port = smtp_port->valueint;
        if (port < 1 || port > 65535) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SMTP port (must be 1-65535)");
            return ESP_FAIL;
        }
        config.smtp_port = (uint16_t)port;
    }

    // Update SMTP username
    if (cJSON_IsString(smtp_username) && smtp_username->valuestring != NULL) {
        if (strlen(smtp_username->valuestring) >= sizeof(config.smtp_username)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SMTP username too long");
            return ESP_FAIL;
        }
        strncpy(config.smtp_username, smtp_username->valuestring, sizeof(config.smtp_username) - 1);
        config.smtp_username[sizeof(config.smtp_username) - 1] = '\0';
    }

    // Update SMTP password
    if (cJSON_IsString(smtp_password) && smtp_password->valuestring != NULL) {
        if (strlen(smtp_password->valuestring) >= sizeof(config.smtp_password)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SMTP password too long");
            return ESP_FAIL;
        }
        strncpy(config.smtp_password, smtp_password->valuestring, sizeof(config.smtp_password) - 1);
        config.smtp_password[sizeof(config.smtp_password) - 1] = '\0';
    }

    // Validate and update recipient email
    if (cJSON_IsString(recipient_email) && recipient_email->valuestring != NULL) {
        if (strlen(recipient_email->valuestring) >= sizeof(config.recipient_email)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recipient email too long");
            return ESP_FAIL;
        }
        // Basic email validation - must contain @
        if (strchr(recipient_email->valuestring, '@') == NULL) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid email format");
            return ESP_FAIL;
        }
        strncpy(config.recipient_email, recipient_email->valuestring, sizeof(config.recipient_email) - 1);
        config.recipient_email[sizeof(config.recipient_email) - 1] = '\0';
    }

    // Update enabled flag
    if (cJSON_IsBool(enabled)) {
        config.enabled = cJSON_IsTrue(enabled);
    }

    config.configured = true;

    cJSON_Delete(root);

    // Save configuration
    email_save_config(&config);

    ESP_LOGI(TAG, "Email config updated: server=%s, port=%d, enabled=%d",
             config.smtp_server, config.smtp_port, config.enabled);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"Email configuration saved\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"Email configuration saved\"}"));
    return ESP_OK;
}

// OTA API Handlers
static esp_err_t get_ota_version_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Firmware version (hardcoded for now)
    cJSON_AddStringToObject(root, "version", "v1.0.0");
    cJSON_AddStringToObject(root, "project_name", "sip_doorbell");
    cJSON_AddStringToObject(root, "build_date", __DATE__);
    cJSON_AddStringToObject(root, "build_time", __TIME__);
    cJSON_AddStringToObject(root, "idf_version", IDF_VER);

    // OTA partition info (placeholder values)
    cJSON_AddNumberToObject(root, "ota_partition_size", 1572864);
    cJSON_AddNumberToObject(root, "ota_available_space", 1048576);

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

// System API Handlers
static esp_err_t get_system_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_system_restart_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "System restart requested");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"System restart initiated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"System restart initiated\"}"));

    // Restart after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

static esp_err_t get_system_info_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Chip model (hardcoded for ESP32-S3)
    cJSON_AddStringToObject(root, "chip_model", "ESP32-S3");
    cJSON_AddNumberToObject(root, "chip_revision", 0);
    cJSON_AddNumberToObject(root, "cpu_cores", 2);
    cJSON_AddNumberToObject(root, "cpu_freq_mhz", 240);
    cJSON_AddNumberToObject(root, "flash_size_mb", 8);
    
    // MAC address
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    if (ret == ESP_OK) {
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strcpy(mac_str, "00:00:00:00:00:00");
    }
    cJSON_AddStringToObject(root, "mac_address", mac_str);
    
    // System info
    uint32_t free_heap = esp_get_free_heap_size();
    cJSON_AddNumberToObject(root, "free_heap_bytes", free_heap);
    
    uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    cJSON_AddNumberToObject(root, "uptime_seconds", uptime_ms / 1000);
    
    // Firmware version
    cJSON_AddStringToObject(root, "firmware_version", "v1.0.0");

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

// NTP API Handlers
static esp_err_t get_ntp_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    bool is_synced = ntp_is_synced();
    cJSON_AddBoolToObject(root, "synced", is_synced);
    
    if (is_synced) {
        char time_str[64];
        ntp_get_time_string(time_str, sizeof(time_str));
        cJSON_AddStringToObject(root, "current_time", time_str);
        cJSON_AddNumberToObject(root, "timestamp_ms", (double)ntp_get_timestamp_ms());
    }
    
    cJSON_AddStringToObject(root, "server", ntp_get_server());
    cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_ntp_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "server", ntp_get_server());
    cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_ntp_config_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *ntp_server = cJSON_GetObjectItem(root, "server");
    const cJSON *timezone = cJSON_GetObjectItem(root, "timezone");

    if (!cJSON_IsString(ntp_server) || (ntp_server->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing server");
        return ESP_FAIL;
    }

    const char* tz = (cJSON_IsString(timezone) && timezone->valuestring) ? 
                     timezone->valuestring : NTP_DEFAULT_TIMEZONE;

    ESP_LOGI(TAG, "NTP config update: server=%s, timezone=%s", ntp_server->valuestring, tz);
    
    // Update NTP configuration
    ntp_set_config(ntp_server->valuestring, tz);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"NTP configuration updated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"NTP configuration updated\"}"));
    return ESP_OK;
}

static esp_err_t post_ntp_sync_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Manual NTP sync requested");
    
    ntp_force_sync();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"NTP sync initiated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"NTP sync initiated\"}"));
    return ESP_OK;
}

// DTMF Security API Handlers
static esp_err_t get_dtmf_security_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    // Get current security configuration
    dtmf_security_config_t config;
    dtmf_get_security_config(&config);

    cJSON_AddBoolToObject(root, "pin_enabled", config.pin_enabled);
    cJSON_AddStringToObject(root, "pin_code", config.pin_code);
    cJSON_AddNumberToObject(root, "timeout_ms", config.timeout_ms);
    cJSON_AddNumberToObject(root, "max_attempts", config.max_attempts);

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_dtmf_security_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    // Parse configuration from JSON
    dtmf_security_config_t config;
    dtmf_get_security_config(&config);  // Start with current config

    const cJSON *pin_enabled = cJSON_GetObjectItem(root, "pin_enabled");
    const cJSON *pin_code = cJSON_GetObjectItem(root, "pin_code");
    const cJSON *timeout_ms = cJSON_GetObjectItem(root, "timeout_ms");
    const cJSON *max_attempts = cJSON_GetObjectItem(root, "max_attempts");

    // Update fields if provided
    if (cJSON_IsBool(pin_enabled)) {
        config.pin_enabled = cJSON_IsTrue(pin_enabled);
    }

    if (cJSON_IsString(pin_code) && (pin_code->valuestring != NULL)) {
        // Validate PIN format (digits only, 1-8 chars)
        size_t pin_len = strlen(pin_code->valuestring);
        if (pin_len < 1 || pin_len > 8) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "PIN must be 1-8 characters");
            return ESP_FAIL;
        }
        for (size_t i = 0; i < pin_len; i++) {
            if (pin_code->valuestring[i] < '0' || pin_code->valuestring[i] > '9') {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "PIN must contain only digits");
                return ESP_FAIL;
            }
        }
        strncpy(config.pin_code, pin_code->valuestring, sizeof(config.pin_code) - 1);
        config.pin_code[sizeof(config.pin_code) - 1] = '\0';
    }

    if (cJSON_IsNumber(timeout_ms)) {
        uint32_t timeout = (uint32_t)timeout_ms->valueint;
        // Validate timeout range (5000-30000 ms)
        if (timeout < 5000 || timeout > 30000) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Timeout must be 5000-30000 ms");
            return ESP_FAIL;
        }
        config.timeout_ms = timeout;
    }

    if (cJSON_IsNumber(max_attempts)) {
        config.max_attempts = (uint8_t)max_attempts->valueint;
    }

    cJSON_Delete(root);

    // Save configuration
    dtmf_save_security_config(&config);

    ESP_LOGI(TAG, "DTMF security config updated: PIN %s, timeout %lu ms, max attempts %d",
             config.pin_enabled ? "enabled" : "disabled",
             config.timeout_ms,
             config.max_attempts);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"DTMF security configuration updated\"}", 
                    strlen("{\"status\":\"success\",\"message\":\"DTMF security configuration updated\"}"));
    return ESP_OK;
}

static esp_err_t get_dtmf_logs_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    
    // Parse query parameter "since"
    char query[64];
    uint64_t since_timestamp = 0;
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(query, "since", param, sizeof(param)) == ESP_OK) {
            since_timestamp = strtoull(param, NULL, 10);
        }
    }
    
    // Allocate log entries on heap to avoid stack overflow
    const int max_entries = 50;
    dtmf_security_log_t *entries = malloc(max_entries * sizeof(dtmf_security_log_t));
    if (!entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    int count = dtmf_get_security_logs(entries, max_entries, since_timestamp);
    
    cJSON *root = cJSON_CreateObject();
    cJSON *logs_array = cJSON_CreateArray();
    
    for (int i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        
        // Use double for timestamp to preserve precision in JSON
        cJSON_AddNumberToObject(entry, "timestamp", (double)entries[i].timestamp);
        
        // Add type as string
        const char* type_str;
        switch (entries[i].type) {
            case CMD_DOOR_OPEN:
                type_str = "door_open";
                break;
            case CMD_LIGHT_TOGGLE:
                type_str = "light_toggle";
                break;
            case CMD_CONFIG_CHANGE:
                type_str = "config_change";
                break;
            case CMD_INVALID:
            default:
                type_str = "invalid";
                break;
        }
        cJSON_AddStringToObject(entry, "type", type_str);
        
        // Add success/failure
        cJSON_AddBoolToObject(entry, "success", entries[i].success);
        
        // Add command
        cJSON_AddStringToObject(entry, "command", entries[i].command);
        
        // Add action (same as type for successful commands)
        if (entries[i].success) {
            cJSON_AddStringToObject(entry, "action", type_str);
        } else {
            cJSON_AddStringToObject(entry, "action", "none");
        }
        
        // Add caller ID
        cJSON_AddStringToObject(entry, "caller", entries[i].caller_id);
        
        // Add reason (for failures)
        if (!entries[i].success && entries[i].reason[0] != '\0') {
            cJSON_AddStringToObject(entry, "reason", entries[i].reason);
        }
        
        cJSON_AddItemToArray(logs_array, entry);
    }
    
    cJSON_AddItemToObject(root, "logs", logs_array);
    cJSON_AddNumberToObject(root, "count", count);
    
    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    free(entries);
    return ESP_OK;
}

// Hardware Test API Handlers
static esp_err_t post_hardware_test_doorbell_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[256];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *bell = cJSON_GetObjectItem(root, "bell");
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    if (!cJSON_IsNumber(bell)) {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Invalid or missing bell number");
    } else {
        int bell_num = bell->valueint;
        doorbell_t doorbell = (bell_num == 1) ? DOORBELL_1 : DOORBELL_2;
        
        esp_err_t result = hardware_test_doorbell(doorbell);
        
        if (result == ESP_OK) {
            cJSON_AddBoolToObject(response, "success", true);
            char msg[64];
            snprintf(msg, sizeof(msg), "Doorbell %d test triggered", bell_num);
            cJSON_AddStringToObject(response, "message", msg);
            ESP_LOGI(TAG, "Doorbell %d test executed", bell_num);
        } else {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "message", "Invalid bell number (must be 1 or 2)");
        }
    }

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_hardware_test_door_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[256];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *duration = cJSON_GetObjectItem(root, "duration");
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    if (!cJSON_IsNumber(duration)) {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Invalid or missing duration");
    } else {
        uint32_t duration_ms = (uint32_t)duration->valueint;
        
        esp_err_t result = hardware_test_door_opener(duration_ms);
        
        if (result == ESP_OK) {
            cJSON_AddBoolToObject(response, "success", true);
            cJSON_AddNumberToObject(response, "duration", duration_ms);
            char msg[64];
            snprintf(msg, sizeof(msg), "Door opener activated for %lu ms", duration_ms);
            cJSON_AddStringToObject(response, "message", msg);
            ESP_LOGI(TAG, "Door opener test: %lu ms", duration_ms);
        } else if (result == ESP_ERR_INVALID_ARG) {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "message", "Duration must be between 1000 and 10000 ms");
        } else if (result == ESP_ERR_INVALID_STATE) {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "message", "Door opener test already in progress");
        } else {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "message", "Door opener test failed");
        }
    }

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_hardware_test_light_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    bool new_state = false;
    esp_err_t result = hardware_test_light_toggle(&new_state);
    
    if (result == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "state", new_state ? "on" : "off");
        ESP_LOGI(TAG, "Light relay toggled: %s", new_state ? "on" : "off");
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Light toggle failed");
    }

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    return ESP_OK;
}

static esp_err_t get_hardware_state_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    hardware_state_t state;
    hardware_test_get_state(&state);
    
    cJSON_AddBoolToObject(response, "door_relay_active", state.door_relay_active);
    cJSON_AddBoolToObject(response, "light_relay_active", state.light_relay_active);
    cJSON_AddBoolToObject(response, "bell1_pressed", state.bell1_pressed);
    cJSON_AddBoolToObject(response, "bell2_pressed", state.bell2_pressed);
    cJSON_AddNumberToObject(response, "door_relay_remaining_ms", state.door_relay_remaining_ms);

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    return ESP_OK;
}

static esp_err_t post_hardware_test_stop_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();

    esp_err_t result = hardware_test_stop_all();
    
    if (result == ESP_OK) {
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "All tests stopped");
        ESP_LOGI(TAG, "Emergency stop executed");
    } else {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "message", "Emergency stop failed");
    }

    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    return ESP_OK;
}

// Certificate Management API Handlers
static esp_err_t get_cert_info_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    
    // Check if certificate exists
    if (!cert_exists()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "{\"error\":\"No certificate found\"}", -1);
        return ESP_OK;
    }
    
    // Get certificate information
    cert_info_t info;
    esp_err_t err = cert_get_info(&info);
    
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "{\"error\":\"Failed to retrieve certificate information\"}", -1);
        return ESP_FAIL;
    }
    
    // Build JSON response
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "exists", true);
    cJSON_AddBoolToObject(root, "is_self_signed", info.is_self_signed);
    cJSON_AddStringToObject(root, "common_name", info.common_name);
    cJSON_AddStringToObject(root, "issuer", info.issuer);
    cJSON_AddStringToObject(root, "not_before", info.not_before);
    cJSON_AddStringToObject(root, "not_after", info.not_after);
    cJSON_AddNumberToObject(root, "days_until_expiry", info.days_until_expiry);
    cJSON_AddBoolToObject(root, "is_expired", info.is_expired);
    cJSON_AddBoolToObject(root, "is_expiring_soon", info.is_expiring_soon);
    
    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t post_cert_upload_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Allocate buffer for request body
    size_t content_len = req->content_len;
    if (content_len > 16384) {  // 16KB max for certificate upload
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too large (max 16KB)");
        return ESP_FAIL;
    }
    
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    // Receive request body
    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    const cJSON *cert_pem = cJSON_GetObjectItem(root, "certificate");
    const cJSON *key_pem = cJSON_GetObjectItem(root, "private_key");
    const cJSON *chain_pem = cJSON_GetObjectItem(root, "chain");
    
    // Validate required fields
    if (!cJSON_IsString(cert_pem) || (cert_pem->valuestring == NULL) ||
        !cJSON_IsString(key_pem) || (key_pem->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Missing certificate or private_key\"}", -1);
        return ESP_FAIL;
    }
    
    // Get chain if provided (optional)
    const char* chain_str = NULL;
    if (cJSON_IsString(chain_pem) && chain_pem->valuestring != NULL) {
        chain_str = chain_pem->valuestring;
    }
    
    // Upload certificate
    esp_err_t err = cert_upload_custom(cert_pem->valuestring, key_pem->valuestring, chain_str);
    
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Certificate uploaded successfully. Server restart required for changes to take effect.");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGI(TAG, "Custom certificate uploaded successfully");
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        
        // Provide specific error messages
        if (err == ESP_ERR_INVALID_ARG) {
            cJSON_AddStringToObject(response, "error", "Invalid certificate format or private key does not match certificate");
        } else if (err == ESP_ERR_INVALID_SIZE) {
            cJSON_AddStringToObject(response, "error", "Certificate or key too large");
        } else {
            cJSON_AddStringToObject(response, "error", "Failed to upload certificate");
        }
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGW(TAG, "Certificate upload failed: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

static esp_err_t post_cert_generate_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret;
    int remaining = req->content_len;
    
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
    
    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    const cJSON *common_name = cJSON_GetObjectItem(root, "common_name");
    const cJSON *validity_days = cJSON_GetObjectItem(root, "validity_days");
    
    // Use defaults if not provided
    const char* cn = (cJSON_IsString(common_name) && common_name->valuestring) ? 
                     common_name->valuestring : "doorstation.local";
    uint32_t validity = (cJSON_IsNumber(validity_days)) ? 
                        (uint32_t)validity_days->valueint : 3650;  // Default 10 years
    
    cJSON_Delete(root);
    
    // Validate validity period (1 day to 20 years)
    if (validity < 1 || validity > 7300) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Validity days must be between 1 and 7300 (20 years)\"}", -1);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Generating self-signed certificate: CN=%s, validity=%d days", cn, validity);
    
    // Generate certificate (this may take a few seconds)
    esp_err_t err = cert_generate_self_signed(cn, validity);
    
    httpd_resp_set_type(req, "application/json");
    
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Self-signed certificate generated successfully. Server restart required for changes to take effect.");
        cJSON_AddStringToObject(response, "common_name", cn);
        cJSON_AddNumberToObject(response, "validity_days", validity);
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGI(TAG, "Self-signed certificate generated successfully");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to generate certificate");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGE(TAG, "Certificate generation failed: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

static esp_err_t get_cert_download_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Check if certificate exists
    if (!cert_exists()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"No certificate found\"}", -1);
        return ESP_OK;
    }
    
    // Get certificate PEM
    char* cert_pem = NULL;
    size_t cert_size = 0;
    esp_err_t err = cert_get_pem(&cert_pem, &cert_size);
    
    if (err != ESP_OK || !cert_pem) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Failed to retrieve certificate\"}", -1);
        return ESP_FAIL;
    }
    
    // Set headers for file download
    httpd_resp_set_type(req, "application/x-pem-file");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"certificate.pem\"");
    
    // Send certificate
    httpd_resp_send(req, cert_pem, cert_size - 1);  // -1 to exclude null terminator
    
    free(cert_pem);
    
    ESP_LOGI(TAG, "Certificate downloaded");
    
    return ESP_OK;
}

static esp_err_t delete_cert_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Check if certificate exists
    if (!cert_exists()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"No certificate found\"}", -1);
        return ESP_OK;
    }
    
    // Delete certificate
    esp_err_t err = cert_delete();
    
    httpd_resp_set_type(req, "application/json");
    
    if (err == ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Certificate deleted successfully. A new certificate must be generated or uploaded before restarting the server.");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGI(TAG, "Certificate deleted successfully");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to delete certificate");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);
        
        ESP_LOGE(TAG, "Certificate deletion failed: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

// Authentication API Handlers
static esp_err_t post_auth_login_handler(httpd_req_t *req)
{
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *username = cJSON_GetObjectItem(root, "username");
    const cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(username) || (username->valuestring == NULL) ||
        !cJSON_IsString(password) || (password->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Missing username or password\"}", -1);
        return ESP_FAIL;
    }

    // Get client IP address
    char client_ip[AUTH_IP_ADDRESS_MAX_LEN] = {0};
    
    // Try to get X-Forwarded-For header first (if behind proxy)
    size_t xff_len = httpd_req_get_hdr_value_len(req, "X-Forwarded-For");
    if (xff_len > 0 && xff_len < sizeof(client_ip)) {
        httpd_req_get_hdr_value_str(req, "X-Forwarded-For", client_ip, sizeof(client_ip));
    } else {
        // Fallback to direct connection IP
        // Get socket descriptor and peer address
        int sockfd = httpd_req_to_sockfd(req);
        struct sockaddr_in6 addr;
        socklen_t addr_len = sizeof(addr);
        
        if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) == 0) {
            if (addr.sin6_family == AF_INET) {
                // IPv4
                struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
                inet_ntop(AF_INET, &addr_in->sin_addr, client_ip, sizeof(client_ip));
            } else if (addr.sin6_family == AF_INET6) {
                // IPv6
                inet_ntop(AF_INET6, &addr.sin6_addr, client_ip, sizeof(client_ip));
            }
        }
        
        // If we still don't have an IP, use a placeholder
        if (client_ip[0] == '\0') {
            strncpy(client_ip, "unknown", sizeof(client_ip) - 1);
        }
    }

    // Check if IP is blocked due to rate limiting
    if (auth_is_ip_blocked(client_ip)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Login attempt from blocked IP: %s", client_ip);
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Too many failed attempts. Please try again later.\"}", -1);
        return ESP_FAIL;
    }

    // Authenticate user
    auth_result_t result = auth_login(username->valuestring, password->valuestring, client_ip);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");

    if (result.authenticated) {
        // Set session cookie with security flags
        char cookie[256];
        snprintf(cookie, sizeof(cookie),
                 "session_id=%s; HttpOnly; Secure; SameSite=Strict; Max-Age=%d; Path=/",
                 result.session_id,
                 AUTH_SESSION_TIMEOUT_SECONDS);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);

        // Send success response
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Login successful");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGI(TAG, "User '%s' logged in successfully from %s", username->valuestring, client_ip);
    } else {
        // Record failed attempt
        auth_record_failed_attempt(client_ip);

        // Send error response
        httpd_resp_set_status(req, "401 Unauthorized");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", result.error_message);
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGW(TAG, "Failed login attempt for user '%s' from %s: %s", 
                 username->valuestring, client_ip, result.error_message);
    }

    return ESP_OK;
}

static esp_err_t post_auth_logout_handler(httpd_req_t *req)
{
    // Extract session ID from cookie
    char session_id[AUTH_SESSION_ID_SIZE] = {0};
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    
    if (cookie_len > 0) {
        char* cookie_str = malloc(cookie_len + 1);
        if (cookie_str) {
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_str, cookie_len + 1) == ESP_OK) {
                // Parse session_id from cookie string
                // Cookie format: "session_id=<value>; other=value"
                char* session_start = strstr(cookie_str, "session_id=");
                if (session_start) {
                    session_start += strlen("session_id=");
                    char* session_end = strchr(session_start, ';');
                    size_t session_len = session_end ? (size_t)(session_end - session_start) : strlen(session_start);
                    
                    if (session_len < AUTH_SESSION_ID_SIZE) {
                        strncpy(session_id, session_start, session_len);
                        session_id[session_len] = '\0';
                    }
                }
            }
            free(cookie_str);
        }
    }
    
    // Invalidate session if found
    if (session_id[0] != '\0') {
        auth_logout(session_id);
        ESP_LOGI(TAG, "User logged out, session invalidated");
    }
    
    // Clear session cookie by setting Max-Age=0
    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "session_id=; HttpOnly; Secure; SameSite=Strict; Max-Age=0; Path=/");
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    
    // Send success response
    httpd_resp_set_type(req, "application/json");
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "Logout successful");
    
    char *json_string = cJSON_Print(response);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(response);
    
    return ESP_OK;
}

static esp_err_t post_auth_set_password_handler(httpd_req_t *req)
{
    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *password = cJSON_GetObjectItem(root, "password");

    if (!cJSON_IsString(password) || (password->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Missing password\"}", -1);
        return ESP_FAIL;
    }

    // Check if password is already set
    if (auth_is_password_set()) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Password already set. Use change-password endpoint.\"}", -1);
        ESP_LOGW(TAG, "Attempt to set password when already configured");
        return ESP_FAIL;
    }

    // Set initial password
    esp_err_t err = auth_set_initial_password(password->valuestring);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");

    if (err == ESP_OK) {
        // Send success response
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Password set successfully");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGI(TAG, "Initial admin password set successfully");
    } else if (err == ESP_ERR_INVALID_ARG) {
        // Password doesn't meet strength requirements
        httpd_resp_set_status(req, "400 Bad Request");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Password does not meet security requirements (min 8 chars, uppercase, lowercase, digit)");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGW(TAG, "Password set failed: does not meet strength requirements");
    } else {
        // Other error
        httpd_resp_set_status(req, "500 Internal Server Error");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to set password");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGE(TAG, "Password set failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static esp_err_t post_auth_change_password_handler(httpd_req_t *req)
{
    // Check authentication - user must be logged in to change password
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }

    char buf[512];
    int ret;
    int remaining = req->content_len;

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

    const cJSON *current_password = cJSON_GetObjectItem(root, "current_password");
    const cJSON *new_password = cJSON_GetObjectItem(root, "new_password");

    if (!cJSON_IsString(current_password) || (current_password->valuestring == NULL) ||
        !cJSON_IsString(new_password) || (new_password->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Missing current_password or new_password\"}", -1);
        return ESP_FAIL;
    }

    // Change password
    esp_err_t err = auth_change_password(current_password->valuestring, new_password->valuestring);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");

    if (err == ESP_OK) {
        // Send success response
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", true);
        cJSON_AddStringToObject(response, "message", "Password changed successfully. All sessions have been invalidated.");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGI(TAG, "Admin password changed successfully, all sessions invalidated");
    } else if (err == ESP_ERR_INVALID_ARG) {
        // Either current password is wrong or new password doesn't meet requirements
        httpd_resp_set_status(req, "400 Bad Request");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Current password is incorrect or new password does not meet security requirements (min 8 chars, uppercase, lowercase, digit)");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGW(TAG, "Password change failed: invalid current password or weak new password");
    } else {
        // Other error
        httpd_resp_set_status(req, "500 Internal Server Error");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "Failed to change password");
        
        char *json_string = cJSON_Print(response);
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
        cJSON_Delete(response);

        ESP_LOGE(TAG, "Password change failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    const size_t index_html_size = (uintptr_t)index_html_end - (uintptr_t)index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static esp_err_t documentation_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    const size_t documentation_html_size = (uintptr_t)documentation_html_end - (uintptr_t)documentation_html_start;
    httpd_resp_send(req, (const char *)documentation_html_start, documentation_html_size);
    return ESP_OK;
}

static esp_err_t login_handler(httpd_req_t *req)
{
    // Login page is public - no authentication required
    httpd_resp_set_type(req, "text/html");
    const size_t login_html_size = (uintptr_t)login_html_end - (uintptr_t)login_html_start;
    httpd_resp_send(req, (const char *)login_html_start, login_html_size);
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

static const httpd_uri_t sip_test_call_uri = {
    .uri       = "/api/sip/testcall",
    .method    = HTTP_POST,
    .handler   = post_sip_test_call_handler,
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

// Authentication API URI handlers
static const httpd_uri_t auth_login_uri = {
    .uri = "/api/auth/login",
    .method = HTTP_POST,
    .handler = post_auth_login_handler,
    .user_ctx = NULL
};

static const httpd_uri_t auth_logout_uri = {
    .uri = "/api/auth/logout",
    .method = HTTP_POST,
    .handler = post_auth_logout_handler,
    .user_ctx = NULL
};

static const httpd_uri_t auth_set_password_uri = {
    .uri = "/api/auth/set-password",
    .method = HTTP_POST,
    .handler = post_auth_set_password_handler,
    .user_ctx = NULL
};

static const httpd_uri_t auth_change_password_uri = {
    .uri = "/api/auth/change-password",
    .method = HTTP_POST,
    .handler = post_auth_change_password_handler,
    .user_ctx = NULL
};

// Certificate Management API URI handlers
static const httpd_uri_t cert_info_uri = {
    .uri = "/api/cert/info",
    .method = HTTP_GET,
    .handler = get_cert_info_handler,
    .user_ctx = NULL
};

static const httpd_uri_t cert_upload_uri = {
    .uri = "/api/cert/upload",
    .method = HTTP_POST,
    .handler = post_cert_upload_handler,
    .user_ctx = NULL
};

static const httpd_uri_t cert_generate_uri = {
    .uri = "/api/cert/generate",
    .method = HTTP_POST,
    .handler = post_cert_generate_handler,
    .user_ctx = NULL
};

static const httpd_uri_t cert_download_uri = {
    .uri = "/api/cert/download",
    .method = HTTP_GET,
    .handler = get_cert_download_handler,
    .user_ctx = NULL
};

static const httpd_uri_t cert_delete_uri = {
    .uri = "/api/cert",
    .method = HTTP_DELETE,
    .handler = delete_cert_handler,
    .user_ctx = NULL
};

// URI handlers
static const httpd_uri_t root_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL
};

static const httpd_uri_t documentation_uri = {
    .uri = "/documentation.html", .method = HTTP_GET, .handler = documentation_handler, .user_ctx = NULL
};

static const httpd_uri_t login_uri = {
    .uri = "/login.html", .method = HTTP_GET, .handler = login_handler, .user_ctx = NULL
};

static const httpd_uri_t wifi_config_get_uri = {
    .uri = "/api/wifi/config", .method = HTTP_GET, .handler = get_wifi_config_handler, .user_ctx = NULL
};

static const httpd_uri_t wifi_config_post_uri = {
    .uri = "/api/wifi/config", .method = HTTP_POST, .handler = post_wifi_config_handler, .user_ctx = NULL
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

static const httpd_uri_t network_ip_uri = {
    .uri = "/api/network/ip", .method = HTTP_GET, .handler = get_network_ip_handler, .user_ctx = NULL
};

static const httpd_uri_t email_config_get_uri = {
    .uri = "/api/email/config", .method = HTTP_GET, .handler = get_email_config_handler, .user_ctx = NULL
};

static const httpd_uri_t email_config_post_uri = {
    .uri = "/api/email/config", .method = HTTP_POST, .handler = post_email_config_handler, .user_ctx = NULL
};

static const httpd_uri_t ota_version_uri = {
    .uri = "/api/ota/version", .method = HTTP_GET, .handler = get_ota_version_handler, .user_ctx = NULL
};

static const httpd_uri_t system_status_uri = {
    .uri = "/api/system/status", .method = HTTP_GET, .handler = get_system_status_handler, .user_ctx = NULL
};

static const httpd_uri_t system_restart_uri = {
    .uri = "/api/system/restart", .method = HTTP_POST, .handler = post_system_restart_handler, .user_ctx = NULL
};

static const httpd_uri_t system_info_uri = {
    .uri = "/api/system/info", .method = HTTP_GET, .handler = get_system_info_handler, .user_ctx = NULL
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

static const httpd_uri_t dtmf_security_get_uri = {
    .uri = "/api/dtmf/security", .method = HTTP_GET, .handler = get_dtmf_security_handler, .user_ctx = NULL
};

static const httpd_uri_t dtmf_security_post_uri = {
    .uri = "/api/dtmf/security", .method = HTTP_POST, .handler = post_dtmf_security_handler, .user_ctx = NULL
};

static const httpd_uri_t dtmf_logs_uri = {
    .uri = "/api/dtmf/logs", .method = HTTP_GET, .handler = get_dtmf_logs_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_test_doorbell_uri = {
    .uri = "/api/hardware/test/doorbell", .method = HTTP_POST, .handler = post_hardware_test_doorbell_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_test_door_uri = {
    .uri = "/api/hardware/test/door", .method = HTTP_POST, .handler = post_hardware_test_door_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_test_light_uri = {
    .uri = "/api/hardware/test/light", .method = HTTP_POST, .handler = post_hardware_test_light_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_state_uri = {
    .uri = "/api/hardware/state", .method = HTTP_GET, .handler = get_hardware_state_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_status_uri = {
    .uri = "/api/hardware/status", .method = HTTP_GET, .handler = get_hardware_state_handler, .user_ctx = NULL
};

static const httpd_uri_t hardware_test_stop_uri = {
    .uri = "/api/hardware/test/stop", .method = HTTP_POST, .handler = post_hardware_test_stop_handler, .user_ctx = NULL
};

// HTTP to HTTPS redirect handler (used as error handler for 404)
static esp_err_t http_redirect_handler(httpd_req_t *req, httpd_err_code_t err __attribute__((unused)))
{
    // Get the Host header to construct the HTTPS URL
    char host[128] = {0};
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    
    if (host_len > 0 && host_len < sizeof(host)) {
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    } else {
        // Fallback to IP address if Host header is missing
        strcpy(host, "192.168.4.1");  // Default AP IP
    }
    
    // Construct HTTPS URL with sufficient buffer size
    // Max: "https://" (8) + host (127) + uri (512) + null (1) = 648 bytes
    char https_url[700];
    snprintf(https_url, sizeof(https_url), "https://%s%s", host, req->uri);
    
    // Send 301 Moved Permanently redirect
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", https_url);
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "HTTP redirect: %s -> %s", req->uri, https_url);
    
    return ESP_OK;
}

// Helper function to load certificate and key from NVS
static esp_err_t load_cert_and_key(char** cert_pem, size_t* cert_size, char** key_pem, size_t* key_size)
{
    esp_err_t err;
    
    // Open NVS
    nvs_handle_t nvs_handle;
    err = nvs_open("cert", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for certificate: %s", esp_err_to_name(err));
        return err;
    }
    
    // Get certificate size
    size_t cert_required_size = 0;
    err = nvs_get_blob(nvs_handle, "cert_pem", NULL, &cert_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Get key size
    size_t key_required_size = 0;
    err = nvs_get_blob(nvs_handle, "key_pem", NULL, &key_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get key size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Allocate memory for certificate
    *cert_pem = (char*)malloc(cert_required_size);
    if (!*cert_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificate");
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate memory for key
    *key_pem = (char*)malloc(key_required_size);
    if (!*key_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for key");
        free(*cert_pem);
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Read certificate
    err = nvs_get_blob(nvs_handle, "cert_pem", *cert_pem, &cert_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
        free(*cert_pem);
        free(*key_pem);
        nvs_close(nvs_handle);
        return err;
    }
    
    // Read key
    err = nvs_get_blob(nvs_handle, "key_pem", *key_pem, &key_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read key: %s", esp_err_to_name(err));
        free(*cert_pem);
        free(*key_pem);
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    *cert_size = cert_required_size;
    *key_size = key_required_size;
    
    ESP_LOGI(TAG, "Certificate and key loaded from NVS (cert: %d bytes, key: %d bytes)", 
             cert_required_size, key_required_size);
    
    return ESP_OK;
}

void web_server_start(void)
{
    // Reduce log verbosity for TLS handshake errors (expected with self-signed certs)
    // Browsers will reject self-signed certificates, causing repeated handshake failures
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);     // Suppress TLS handshake errors
    esp_log_level_set("esp_https_server", ESP_LOG_NONE);    // Suppress HTTPS server session errors
    esp_log_level_set("httpd", ESP_LOG_NONE);               // Suppress HTTP daemon connection errors
    
    // Log that we've started (since we suppressed the component logs)
    ESP_LOGI(TAG, "Starting HTTPS server on port 443...");
    
    // Load certificate and key from NVS (certificate should already be initialized in main.c)
    char* cert_pem = NULL;
    char* key_pem = NULL;
    size_t cert_size = 0;
    size_t key_size = 0;
    
    esp_err_t err = load_cert_and_key(&cert_pem, &cert_size, &key_pem, &key_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load certificate and key");
        return;
    }
    
    // Configure HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.max_uri_handlers = 50;  // Increased for auth and certificate endpoints
    config.httpd.server_port = 443;
    config.httpd.ctrl_port = 32768;
    
    // Set certificate and key
    config.servercert = (const uint8_t*)cert_pem;
    config.servercert_len = cert_size;
    config.prvtkey_pem = (const uint8_t*)key_pem;
    config.prvtkey_len = key_size;
    
    // Configure TLS 1.2 as minimum (this is typically the default)
    // The mbedtls configuration handles the TLS version and cipher suites
    
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        // Free the allocated memory after server starts (it makes internal copies)
        free(cert_pem);
        free(key_pem);
        // Register all URI handlers
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &documentation_uri);
        httpd_register_uri_handler(server, &login_uri);
        
        // Authentication API endpoints
        httpd_register_uri_handler(server, &auth_login_uri);
        httpd_register_uri_handler(server, &auth_logout_uri);
        httpd_register_uri_handler(server, &auth_set_password_uri);
        httpd_register_uri_handler(server, &auth_change_password_uri);
        
        // Certificate Management API endpoints
        httpd_register_uri_handler(server, &cert_info_uri);
        httpd_register_uri_handler(server, &cert_upload_uri);
        httpd_register_uri_handler(server, &cert_generate_uri);
        httpd_register_uri_handler(server, &cert_download_uri);
        httpd_register_uri_handler(server, &cert_delete_uri);
        
        // SIP API endpoints
        httpd_register_uri_handler(server, &sip_status_uri);
        httpd_register_uri_handler(server, &sip_config_get_uri);
        httpd_register_uri_handler(server, &sip_config_post_uri);
        httpd_register_uri_handler(server, &sip_test_uri);
        httpd_register_uri_handler(server, &sip_test_call_uri);
        httpd_register_uri_handler(server, &sip_log_uri);
        httpd_register_uri_handler(server, &sip_connect_uri);
        httpd_register_uri_handler(server, &sip_disconnect_uri);
        
        // WiFi API endpoints
        httpd_register_uri_handler(server, &wifi_config_get_uri);
        httpd_register_uri_handler(server, &wifi_config_post_uri);
        httpd_register_uri_handler(server, &wifi_status_uri);
        httpd_register_uri_handler(server, &wifi_scan_uri);
        httpd_register_uri_handler(server, &wifi_connect_uri);
        
        // Network API endpoints
        httpd_register_uri_handler(server, &network_ip_uri);
        
        // Email API endpoints
        httpd_register_uri_handler(server, &email_config_get_uri);
        httpd_register_uri_handler(server, &email_config_post_uri);
        
        // OTA API endpoints
        httpd_register_uri_handler(server, &ota_version_uri);
        
        // System API endpoints
        httpd_register_uri_handler(server, &system_status_uri);
        httpd_register_uri_handler(server, &system_restart_uri);
        httpd_register_uri_handler(server, &system_info_uri);
        
        // NTP API endpoints
        httpd_register_uri_handler(server, &ntp_status_uri);
        httpd_register_uri_handler(server, &ntp_config_get_uri);
        httpd_register_uri_handler(server, &ntp_config_post_uri);
        httpd_register_uri_handler(server, &ntp_sync_uri);
        
        // DTMF Security API endpoints
        httpd_register_uri_handler(server, &dtmf_security_get_uri);
        httpd_register_uri_handler(server, &dtmf_security_post_uri);
        httpd_register_uri_handler(server, &dtmf_logs_uri);
        
        // Hardware Test API endpoints
        httpd_register_uri_handler(server, &hardware_test_doorbell_uri);
        httpd_register_uri_handler(server, &hardware_test_door_uri);
        httpd_register_uri_handler(server, &hardware_test_light_uri);
        httpd_register_uri_handler(server, &hardware_state_uri);
        httpd_register_uri_handler(server, &hardware_status_uri);
        httpd_register_uri_handler(server, &hardware_test_stop_uri);
        
        ESP_LOGI(TAG, "HTTPS server started on port 443 with all API endpoints");
        
        // Start HTTP redirect server on port 80
        httpd_config_t redirect_config = HTTPD_DEFAULT_CONFIG();
        redirect_config.server_port = 80;
        redirect_config.ctrl_port = 32769;  // Different control port
        redirect_config.max_uri_handlers = 1;  // Need at least 1 for error handler
        
        esp_err_t redirect_err = httpd_start(&redirect_server, &redirect_config);
        if (redirect_err == ESP_OK) {
            // Register error handler for 404 to redirect all requests to HTTPS
            httpd_register_err_handler(redirect_server, HTTPD_404_NOT_FOUND, http_redirect_handler);
            ESP_LOGI(TAG, "HTTP redirect server started on port 80");
        } else {
            ESP_LOGW(TAG, "Failed to start HTTP redirect server on port 80: %s", esp_err_to_name(redirect_err));
            ESP_LOGW(TAG, "HTTP to HTTPS redirect will not be available");
        }
    } else {
        ESP_LOGE(TAG, "Error starting HTTPS server!");
        free(cert_pem);
        free(key_pem);
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTPS server stopped");
    }
    
    if (redirect_server) {
        httpd_stop(redirect_server);
        redirect_server = NULL;
        ESP_LOGI(TAG, "HTTP redirect server stopped");
    }
}
