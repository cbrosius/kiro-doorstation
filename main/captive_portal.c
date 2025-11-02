#include "captive_portal.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "wifi_manager.h"
#include "web_api.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "CAPTIVE_PORTAL";
static httpd_handle_t captive_server = NULL;

// Binary data for captive setup page
extern const uint8_t captive_setup_html_start[] asm("_binary_captive_setup_html_start");
extern const uint8_t captive_setup_html_end[] asm("_binary_captive_setup_html_end");

/**
 * @brief Handler for captive portal setup page
 */
static esp_err_t captive_setup_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Serving captive setup page for URI: %s", req->uri);

    httpd_resp_set_type(req, "text/html");
    const size_t captive_setup_html_size = (uintptr_t)captive_setup_html_end - (uintptr_t)captive_setup_html_start;
    httpd_resp_send(req, (const char *)captive_setup_html_start, captive_setup_html_size);
    return ESP_OK;
}

/**
 * @brief Handler for Android captive portal detection
 */
static esp_err_t captive_generate_204_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Android captive detection: %s", req->uri);

    // Redirect to setup page to reliably trigger captive portal popup
    ESP_LOGI(TAG, "Redirecting to /setup.html to trigger captive portal");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/setup.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Handler for iOS captive portal detection
 */
static esp_err_t captive_hotspot_detect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: iOS captive detection: %s", req->uri);

    // Return a small HTML page that redirects to setup
    const char *redirect_html =
        "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0;url=/setup.html\"></head><body></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, redirect_html, strlen(redirect_html));
    return ESP_OK;
}

/**
 * @brief Handler for Windows NCSI captive portal detection
 */
static esp_err_t captive_ncsi_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Windows NCSI captive detection: %s", req->uri);

    // Return a small HTML page that redirects to setup
    const char *redirect_html =
        "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0;url=/setup.html\"></head><body></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, redirect_html, strlen(redirect_html));
    return ESP_OK;
}

/**
 * @brief Handler for Microsoft connectivity check
 */
static esp_err_t captive_msftconnecttest_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Microsoft connectivity check: %s", req->uri);

    // Return a small HTML page that redirects to setup
    const char *redirect_html =
        "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0;url=/setup.html\"></head><body></body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, redirect_html, strlen(redirect_html));
    return ESP_OK;
}

/**
 * @brief Generic redirect handler for unknown captive portal URLs
 */
static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Unknown captive URL: %s - checking for STA IP redirect", req->uri);

    // Check if we have a tested STA IP available for redirect
    const char* sta_ip = wifi_get_tested_sta_ip();
    if (sta_ip && strlen(sta_ip) > 0) {
        ESP_LOGI(TAG, "Redirecting to STA IP: %s", sta_ip);
        char redirect_url[64];
        snprintf(redirect_url, sizeof(redirect_url), "http://%s/", sta_ip);

        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", redirect_url);
        httpd_resp_send(req, NULL, 0);

        // Clear the tested IP after redirect to prevent repeated redirects
        wifi_clear_tested_sta_ip();

        // After successful redirect, stop captive portal and transition to STA-only mode
        ESP_LOGI(TAG, "Successful redirect completed, stopping captive portal and transitioning to STA-only mode");
        captive_portal_stop();
        dns_responder_stop();
        wifi_transition_to_sta_mode();

        return ESP_OK;
    }

    // Default: redirect to setup page
    ESP_LOGI(TAG, "No STA IP available, redirecting to setup page");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/setup.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Captive portal wrapper for WiFi config GET handler
 */
static esp_err_t captive_get_wifi_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: WiFi config GET for URI: %s", req->uri);
    // No authentication required for captive portal
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

/**
 * @brief Captive portal wrapper for WiFi config POST handler
 */
static esp_err_t captive_post_wifi_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: WiFi config POST for URI: %s", req->uri);
    // No authentication required for captive portal
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

    ESP_LOGI(TAG, "Captive WiFi config save: SSID=%s", ssid->valuestring);

    // Save WiFi configuration (does not connect)
    wifi_save_config(ssid->valuestring, pwd);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}",
                    strlen("{\"status\":\"success\",\"message\":\"WiFi configuration saved\"}"));
    return ESP_OK;
}

/**
 * @brief Captive portal wrapper for WiFi scan handler
 */
static esp_err_t captive_post_wifi_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: WiFi scan POST for URI: %s", req->uri);
    // No authentication required for captive portal
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    cJSON *networks_array = cJSON_CreateArray();
    if (!networks_array) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Captive WiFi scan started");

    // Trigger WiFi scan
    wifi_scan_result_t* scan_results = NULL;
    int network_count = wifi_scan_networks(&scan_results);

    if (network_count > 0 && scan_results != NULL) {
        for (int i = 0; i < network_count; i++) {
            cJSON *network = cJSON_CreateObject();
            if (!network) {
                // Out of memory - return what we have so far
                break;
            }
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
    if (!json_string) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Handler for getting captive portal status (credential testing, STA IP)
 */
esp_err_t captive_get_status_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: Status check for URI: %s", req->uri);

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();

    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    // Add testing status
    cJSON_AddBoolToObject(root, "testing", wifi_is_testing_credentials());

    // Add STA IP if available
    const char* sta_ip = wifi_get_tested_sta_ip();
    if (sta_ip) {
        cJSON_AddStringToObject(root, "sta_ip", sta_ip);
        cJSON_AddBoolToObject(root, "ready", true);
    } else {
        cJSON_AddNullToObject(root, "sta_ip");
        cJSON_AddBoolToObject(root, "ready", false);
    }

    char *json_string = cJSON_Print(root);
    if (!json_string) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Captive portal wrapper for WiFi connect handler
 */
static esp_err_t captive_post_wifi_connect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP REQUEST: WiFi connect POST for URI: %s", req->uri);
    // No authentication required for captive portal
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

    ESP_LOGI(TAG, "Captive WiFi connect: SSID=%s", ssid->valuestring);

    // Save WiFi configuration
    wifi_save_config(ssid->valuestring, pwd);

    // Start parallel credential testing instead of immediate connection
    if (wifi_test_credentials(ssid->valuestring, pwd)) {
        ESP_LOGI(TAG, "Parallel credential testing started successfully");
        cJSON_Delete(root);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"testing\",\"message\":\"Testing WiFi credentials...\"}",
                        strlen("{\"status\":\"testing\",\"message\":\"Testing WiFi credentials...\"}"));
    } else {
        ESP_LOGE(TAG, "Failed to start credential testing, staying in APSTA mode for retry");
        cJSON_Delete(root);

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Failed to start credential testing\"}",
                        strlen("{\"status\":\"error\",\"message\":\"Failed to start credential testing\"}"));
    }

    return ESP_OK;
}

bool captive_portal_start(void)
{
    ESP_LOGI(TAG, "Starting captive portal HTTP server on port 80");

    // Configure lightweight HTTP server for captive portal
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32770;  // Different control port from main server
    config.max_uri_handlers = 20;  // Increased for all captive detection endpoints
    config.keep_alive_enable = true;
    config.keep_alive_idle = 10;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 5;

    // The HTTP server should automatically bind to all interfaces in APSTA mode
    // This should allow it to receive requests from both STA and AP clients
    ESP_LOGI(TAG, "HTTP server starting - binding to all interfaces in APSTA mode");

    esp_err_t err = httpd_start(&captive_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start captive portal server: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Captive portal HTTP server started on port %d, handle: %p", 80, captive_server);

    // Test if server is actually listening by checking the handle
    if (captive_server == NULL) {
        ESP_LOGE(TAG, "HTTP server handle is NULL after start!");
        return false;
    }

    // Log server information
    ESP_LOGI(TAG, "HTTP server is now listening for connections on AP interface");

    // Register captive portal endpoints
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
        .user_ctx = NULL
    };

    httpd_uri_t setup_uri = {
        .uri = "/setup.html",
        .method = HTTP_GET,
        .handler = captive_setup_handler,
        .user_ctx = NULL
    };

    httpd_uri_t generate_204_uri = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = captive_generate_204_handler,
        .user_ctx = NULL
    };

    httpd_uri_t hotspot_detect_uri = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = captive_hotspot_detect_handler,
        .user_ctx = NULL
    };

    httpd_uri_t ncsi_uri = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = captive_ncsi_handler,
        .user_ctx = NULL
    };

    httpd_uri_t msft_connect_uri = {
        .uri = "/connecttest.txt",
        .method = HTTP_GET,
        .handler = captive_msftconnecttest_handler,
        .user_ctx = NULL
    };

    // Register handlers
    ESP_LOGI(TAG, "Registering HTTP URI handlers...");
    esp_err_t reg_err;

    reg_err = httpd_register_uri_handler(captive_server, &root_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register root handler: %s", esp_err_to_name(reg_err));

    reg_err = httpd_register_uri_handler(captive_server, &setup_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register setup handler: %s", esp_err_to_name(reg_err));

    reg_err = httpd_register_uri_handler(captive_server, &generate_204_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register generate_204 handler: %s", esp_err_to_name(reg_err));

    reg_err = httpd_register_uri_handler(captive_server, &hotspot_detect_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register hotspot_detect handler: %s", esp_err_to_name(reg_err));

    reg_err = httpd_register_uri_handler(captive_server, &ncsi_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register ncsi handler: %s", esp_err_to_name(reg_err));

    reg_err = httpd_register_uri_handler(captive_server, &msft_connect_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register msft_connect handler: %s", esp_err_to_name(reg_err));

    // Register additional OS-specific captive detection handlers
    // Android variants
    httpd_uri_t generate_204_alt_uri = {
        .uri = "/gen_204",
        .method = HTTP_GET,
        .handler = captive_generate_204_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(captive_server, &generate_204_alt_uri);

    // iOS/macOS variants
    httpd_uri_t hotspot_detect_alt_uri = {
        .uri = "/hotspotdetect.html",
        .method = HTTP_GET,
        .handler = captive_hotspot_detect_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(captive_server, &hotspot_detect_alt_uri);

    // Note: /ncsi.txt is already registered above as ncsi_uri

    // Additional Windows/Microsoft endpoints
    httpd_uri_t msft_redirect_uri = {
        .uri = "/redirect",
        .method = HTTP_GET,
        .handler = captive_msftconnecttest_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(captive_server, &msft_redirect_uri);

    // Common browser connectivity checks
    httpd_uri_t success_txt_uri = {
        .uri = "/success.txt",
        .method = HTTP_GET,
        .handler = captive_ncsi_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(captive_server, &success_txt_uri);

    // Register a catch-all handler for any other URLs to redirect to setup
    httpd_uri_t catch_all_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
        .user_ctx = NULL
    };
    reg_err = httpd_register_uri_handler(captive_server, &catch_all_uri);
    if (reg_err != ESP_OK) ESP_LOGE(TAG, "Failed to register catch-all handler: %s", esp_err_to_name(reg_err));

    ESP_LOGI(TAG, "All HTTP URI handlers registered successfully");

    // Register specific handler for connectivitycheck.gstatic.com/generate_204
    // NOTE: This is a duplicate registration of /generate_204 - removing to avoid warning
    // httpd_uri_t gstatic_generate_204_uri = {
    //     .uri = "/generate_204",
    //     .method = HTTP_GET,
    //     .handler = captive_generate_204_handler,
    //     .user_ctx = NULL
    // };
    // httpd_register_uri_handler(captive_server, &gstatic_generate_204_uri);

    // Register WiFi API handlers (no authentication required)
    captive_api_register_handlers(captive_server);

    // Register status handler for credential testing feedback
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = captive_get_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(captive_server, &status_uri);

    // Skip test TCP server for now - focus on fixing the main HTTP server issue
    ESP_LOGI(TAG, "Skipping test TCP server - focusing on HTTP server fix");

    ESP_LOGI(TAG, "Captive portal server started successfully");
    return true;
}

void captive_portal_stop(void)
{
    if (captive_server) {
        ESP_LOGI(TAG, "Stopping captive portal server");
        httpd_stop(captive_server);
        captive_server = NULL;
        ESP_LOGI(TAG, "Captive portal server stopped");
    }

    // Test TCP server was skipped
}

void captive_api_register_handlers(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering captive WiFi API handlers");

    // Reuse existing WiFi API handlers from web_api.c
    // These are registered without authentication for captive portal use

    // WiFi config GET handler
    httpd_uri_t wifi_config_get_uri = {
        .uri = "/api/wifi/config",
        .method = HTTP_GET,
        .handler = captive_get_wifi_config_handler,
        .user_ctx = NULL
    };

    // WiFi config POST handler
    httpd_uri_t wifi_config_post_uri = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = captive_post_wifi_config_handler,
        .user_ctx = NULL
    };

    // WiFi scan handler
    httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_POST,
        .handler = captive_post_wifi_scan_handler,
        .user_ctx = NULL
    };

    // WiFi connect handler
    httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = captive_post_wifi_connect_handler,
        .user_ctx = NULL
    };

    // Register handlers on captive server
    httpd_register_uri_handler(server, &wifi_config_get_uri);
    httpd_register_uri_handler(server, &wifi_config_post_uri);
    httpd_register_uri_handler(server, &wifi_scan_uri);
    httpd_register_uri_handler(server, &wifi_connect_uri);

    ESP_LOGI(TAG, "Captive WiFi API handlers registered");
}
