#include "web_server.h"
#include "sip_client.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "dtmf_decoder.h"
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
    
    // Use sip_get_status which returns complete status including state name
    char status_buffer[512];
    sip_get_status(status_buffer, sizeof(status_buffer));
    
    httpd_resp_send(req, status_buffer, strlen(status_buffer));
    return ESP_OK;
}

static esp_err_t get_sip_config_handler(httpd_req_t *req)
{
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t post_wifi_connect_handler(httpd_req_t *req)
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

    char *json_string = cJSON_Print(root);
    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
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

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    const size_t index_html_size = (uintptr_t)index_html_end - (uintptr_t)index_html_start;
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

static const httpd_uri_t dtmf_security_get_uri = {
    .uri = "/api/dtmf/security", .method = HTTP_GET, .handler = get_dtmf_security_handler, .user_ctx = NULL
};

static const httpd_uri_t dtmf_security_post_uri = {
    .uri = "/api/dtmf/security", .method = HTTP_POST, .handler = post_dtmf_security_handler, .user_ctx = NULL
};

static const httpd_uri_t dtmf_logs_uri = {
    .uri = "/api/dtmf/logs", .method = HTTP_GET, .handler = get_dtmf_logs_handler, .user_ctx = NULL
};

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24; // Increase handler limit for DTMF endpoints

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register all URI handlers
        httpd_register_uri_handler(server, &root_uri);
        
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
        
        // DTMF Security API endpoints
        httpd_register_uri_handler(server, &dtmf_security_get_uri);
        httpd_register_uri_handler(server, &dtmf_security_post_uri);
        httpd_register_uri_handler(server, &dtmf_logs_uri);
        
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
