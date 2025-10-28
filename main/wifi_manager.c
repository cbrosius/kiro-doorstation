#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include "captive_portal.h"
#include "dns_responder.h"
#include <string.h>

static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool is_connected = false;
static esp_netif_t* sta_netif = NULL;
static esp_netif_t* ap_netif = NULL;
static wifi_connection_info_t current_connection = {0};
static int retry_count = 0;
static const int MAX_RETRIES = 10;
static bool is_scanning = false;
static wifi_manager_ap_start_callback_t ap_start_cb = NULL;

// Parallel credential testing variables
static bool is_testing_credentials = false;
static char tested_sta_ip[16] = {0};
static TaskHandle_t credential_test_task = NULL;
static const int CREDENTIAL_TEST_TIMEOUT_MS = 30000; // 30 seconds

// ESPHome-style scan results storage
static wifi_scan_result_t scan_results[MAX_SCAN_RESULTS];
static int scan_results_count = 0;
static bool scan_results_valid = false;

// Forward declarations
static void wifi_clear_scan_results(void);
static void credential_test_task_func(void *pvParameters);

void wifi_manager_register_ap_start_callback(wifi_manager_ap_start_callback_t cb)
{
    ap_start_cb = cb;
}

static const char* wifi_reason_to_string(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "Unspecified";
        case WIFI_REASON_AUTH_EXPIRE: return "Auth expire";
        case WIFI_REASON_AUTH_LEAVE: return "Auth leave";
        case WIFI_REASON_ASSOC_EXPIRE: return "Assoc expire";
        case WIFI_REASON_ASSOC_TOOMANY: return "Assoc too many";
        case WIFI_REASON_NOT_AUTHED: return "Not authed";
        case WIFI_REASON_NOT_ASSOCED: return "Not assoced";
        case WIFI_REASON_ASSOC_LEAVE: return "Assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "Assoc not authed";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "Disassoc pwrcap bad";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "Disassoc supchan bad";
        case WIFI_REASON_IE_INVALID: return "IE invalid";
        case WIFI_REASON_MIC_FAILURE: return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "Group key update timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE in 4way differs";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "Group cipher invalid";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "Pairwise cipher invalid";
        case WIFI_REASON_AKMP_INVALID: return "AKMP invalid";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "Unsupp RSN IE version";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "Invalid RSN IE cap";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802.1X auth failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "Cipher suite rejected";
        case WIFI_REASON_INVALID_PMKID: return "Invalid PMKID";
        case WIFI_REASON_BEACON_TIMEOUT: return "Beacon timeout";
        case WIFI_REASON_NO_AP_FOUND: return "No AP found";
        case WIFI_REASON_AUTH_FAIL: return "Auth fail";
        case WIFI_REASON_ASSOC_FAIL: return "Assoc fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL: return "Connection fail";
        case WIFI_REASON_AP_TSF_RESET: return "AP TSF reset";
        case WIFI_REASON_ROAMING: return "Roaming";
        default: return "Unknown";
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "WiFi event: base=%s, id=%d", event_base == WIFI_EVENT ? "WIFI_EVENT" : "IP_EVENT", (int)event_id);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (!is_scanning) {
            ESP_LOGI(TAG, "WiFi STA started, attempting to connect");
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "WiFi STA started during scan, skipping auto-connect");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        retry_count++;
        ESP_LOGW(TAG, "WiFi disconnected (reason: %d - %s), retry %d/%d", disconnected->reason, wifi_reason_to_string(disconnected->reason), retry_count, MAX_RETRIES);
        is_connected = false;

        // Clear connection info
        memset(&current_connection, 0, sizeof(current_connection));

        if (retry_count >= MAX_RETRIES) {
            ESP_LOGW(TAG, "Max retries reached, falling back to AP mode");
            wifi_start_ap_mode();
            if (ap_start_cb) {
                ap_start_cb();
            }
            retry_count = 0; // Reset for future attempts
        } else {
            ESP_LOGI(TAG, "Retrying WiFi connection...");
            esp_wifi_connect();
        }
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected to AP, waiting for IP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP obtained: " IPSTR ", WiFi fully connected", IP2STR(&event->ip_info.ip));
        is_connected = true;
        retry_count = 0; // Reset retry count on successful connection

        // Stop captive portal and DNS responder if they were running
        ESP_LOGI(TAG, "WiFi connected - stopping captive portal and DNS responder");
        captive_portal_stop();
        dns_responder_stop();

        // Update connection info
        current_connection.connected = true;
        snprintf(current_connection.ip_address, sizeof(current_connection.ip_address),
                IPSTR, IP2STR(&event->ip_info.ip));

        // Get SSID and RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(current_connection.ssid, (char*)ap_info.ssid, sizeof(current_connection.ssid) - 1);
            current_connection.ssid[sizeof(current_connection.ssid) - 1] = '\0';
            current_connection.rssi = ap_info.rssi;
        }

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "IP lost, WiFi connection may be unstable");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "WiFi scan completed, processing results...");

        uint16_t ap_count = 0;
        esp_err_t err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
            is_scanning = false;
            return;
        }

        ESP_LOGI(TAG, "Scan found %d APs", ap_count);

        if (ap_count > 0) {
            wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
            if (ap_list != NULL) {
                err = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
                if (err == ESP_OK) {
                    // Store results in global array
                    scan_results_count = 0;
                    for (int i = 0; i < ap_count && scan_results_count < MAX_SCAN_RESULTS; i++) {
                        // Skip hidden networks and duplicates
                        if (ap_list[i].ssid[0] == '\0') continue;

                        // Check for duplicates
                        bool is_duplicate = false;
                        for (int j = 0; j < scan_results_count; j++) {
                            if (strcmp((char*)ap_list[i].ssid, scan_results[j].ssid) == 0) {
                                is_duplicate = true;
                                break;
                            }
                        }
                        if (is_duplicate) continue;

                        // Store result
                        strncpy(scan_results[scan_results_count].ssid, (char*)ap_list[i].ssid, WIFI_SSID_MAX_LEN - 1);
                        scan_results[scan_results_count].ssid[WIFI_SSID_MAX_LEN - 1] = '\0';
                        scan_results[scan_results_count].rssi = ap_list[i].rssi;
                        scan_results[scan_results_count].secure = (ap_list[i].authmode != WIFI_AUTH_OPEN);
                        scan_results_count++;

                        ESP_LOGI(TAG, "Stored network: %s (RSSI: %d, Secure: %s)",
                                scan_results[scan_results_count - 1].ssid,
                                scan_results[scan_results_count - 1].rssi,
                                scan_results[scan_results_count - 1].secure ? "yes" : "no");
                    }
                    scan_results_valid = true;
                    ESP_LOGI(TAG, "Stored %d unique scan results", scan_results_count);
                }
                free(ap_list);
            }
        }

        is_scanning = false;

        // ESPHome approach: Keep STA interface active for future scans
        ESP_LOGI(TAG, "ESPHome approach: Scan completed, STA interface ready for future scans");
    }
}



void wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Manager");
    
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Reduce Block Ack window to avoid timer crashes
    cfg.ampdu_rx_enable = 0;  // Disable AMPDU RX to prevent ADDBA/DELBA timer issues
    cfg.ampdu_tx_enable = 0;  // Disable AMPDU TX for consistency
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Disable WiFi power save for better stability under load
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "WiFi power save disabled for system stability");
    ESP_LOGI(TAG, "WiFi AMPDU disabled to prevent Block Ack timer crashes");
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Gespeicherte WiFi-Konfiguration laden
    wifi_manager_config_t saved_config = wifi_load_config();

    if (saved_config.configured) {
        ESP_LOGI(TAG, "Saved WiFi configuration found, but deferring connection until credential testing completes");
        // Don't auto-connect here - let the main loop handle it after checking for credential testing
    } else {
        ESP_LOGI(TAG, "No WiFi configuration found, starting AP mode");
        wifi_start_ap_mode();
        if (ap_start_cb) {
            ap_start_cb();
        }
    }

    ESP_LOGI(TAG, "WiFi Manager initialized");
}

bool wifi_is_connected(void)
{
    return is_connected;
}

void wifi_start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode");

    // Stop WiFi if it's running to ensure a clean state
    esp_wifi_stop();

    // ESPHome approach: Start in APSTA mode to allow STA configuration
    ESP_LOGI(TAG, "ESPHome approach: Starting in APSTA mode for dual-interface support");

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-Doorbell",
            .ssid_len = strlen("ESP32-Doorbell"),
            .password = "doorbell123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    // Start in APSTA mode (ESPHome pattern)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Configure STA with empty config (ESPHome's wifi_sta_pre_setup_())
    wifi_config_t sta_config = {0};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started. SSID: %s", wifi_config.ap.ssid);
}

void wifi_connect_sta(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    retry_count = 0; // Reset retry count when starting new connection

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // Stop WiFi if it's running
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stopped existing WiFi connection");
    } else if (err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "WiFi stop returned: %s", esp_err_to_name(err));
    }
    
    // Set the mode and configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Start WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "WiFi started successfully");
}

void wifi_save_config(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Saving WiFi configuration");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        nvs_set_str(nvs_handle, "password", password);
        nvs_set_u8(nvs_handle, "configured", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi configuration saved");
    } else {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
    }
}

wifi_manager_config_t wifi_load_config(void)
{
    wifi_manager_config_t config = {0};
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        uint8_t configured = 0;
        
        nvs_get_u8(nvs_handle, "configured", &configured);
        if (configured) {
            required_size = sizeof(config.ssid);
            nvs_get_str(nvs_handle, "ssid", config.ssid, &required_size);
            
            required_size = sizeof(config.password);
            nvs_get_str(nvs_handle, "password", config.password, &required_size);
            
            config.configured = true;
        }
        nvs_close(nvs_handle);
    }
    
    return config;
}

void wifi_clear_config(void)
{
    ESP_LOGI(TAG, "Clearing WiFi configuration");

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, "ssid");
        nvs_erase_key(nvs_handle, "password");
        nvs_erase_key(nvs_handle, "configured");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi configuration cleared");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for clearing config: %s", esp_err_to_name(err));
    }
}

// ESPHome-style background scanning
void wifi_start_background_scan(void)
{
    ESP_LOGI(TAG, "Starting background WiFi scan...");

    // Clear previous results
    wifi_clear_scan_results();

    // Set scanning flag to prevent auto-connect
    is_scanning = true;

    // Check current WiFi mode
    wifi_mode_t current_mode;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        is_scanning = false;
        return;
    }

    ESP_LOGI(TAG, "Current WiFi mode: %d", current_mode);

    // Use ESPHome-style scan configuration - simple and effective
    wifi_scan_config_t scan_config = {0};

    // ESPHome approach: STA interface already configured, start scanning
    ESP_LOGI(TAG, "ESPHome approach: Starting scan with pre-configured STA interface...");

    // Start scan using STA interface (ESPHome's method)
    err = esp_wifi_scan_start(&scan_config, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "STA scan failed: %s", esp_err_to_name(err));
        is_scanning = false;
        return;
    }

    ESP_LOGI(TAG, "ESPHome-style scan started successfully");

    ESP_LOGI(TAG, "Background scan started successfully");
    // Note: Results will be processed in the scan done callback
}

void wifi_clear_scan_results(void)
{
    scan_results_count = 0;
    scan_results_valid = false;
    ESP_LOGI(TAG, "Scan results cleared");
}

int wifi_get_scan_results(wifi_scan_result_t* results, int max_results)
{
    if (!scan_results_valid) {
        ESP_LOGW(TAG, "Scan results not valid");
        return 0;
    }

    int count = (scan_results_count < max_results) ? scan_results_count : max_results;
    memcpy(results, scan_results, count * sizeof(wifi_scan_result_t));
    ESP_LOGI(TAG, "Returning %d scan results", count);
    return count;
}

wifi_connection_info_t wifi_get_connection_info(void)
{
    if (is_connected && current_connection.connected) {
        // Return cached connection info for better performance
        return current_connection;
    }
    
    wifi_connection_info_t info = {0};
    
    if (is_connected) {
        // Get current AP info if cache is not available
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(info.ssid, (char*)ap_info.ssid, sizeof(info.ssid) - 1);
            info.ssid[sizeof(info.ssid) - 1] = '\0';
            info.rssi = ap_info.rssi;
        }
        
        // Get IP address
        if (sta_netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
                snprintf(info.ip_address, sizeof(info.ip_address), 
                        IPSTR, IP2STR(&ip_info.ip));
            }
        }
        
        info.connected = true;
    } else {
        strcpy(info.ip_address, "0.0.0.0");
        strcpy(info.ssid, "Not connected");
        info.rssi = 0;
        info.connected = false;
    }
    
    return info;
}

int wifi_scan_networks(wifi_scan_result_t** results)
{
    ESP_LOGI(TAG, "WiFi scan requested via API");

    // Start a new async scan
    wifi_start_background_scan();

    // Wait for scan to complete (typical scan takes 1-3 seconds)
    int max_wait_ms = 5000;  // 5 seconds max
    int wait_interval_ms = 100;
    int waited_ms = 0;

    while (waited_ms < max_wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(wait_interval_ms));
        waited_ms += wait_interval_ms;

        // Check if scan completed
        if (scan_results_valid && !is_scanning) {
            break;
        }
    }

    // Check if we have valid results
    if (!scan_results_valid || scan_results_count == 0) {
        ESP_LOGW(TAG, "Scan timeout or no results after %d ms", waited_ms);
        *results = NULL;
        return 0;
    }

    ESP_LOGI(TAG, "Scan completed with %d results after %d ms", scan_results_count, waited_ms);

    // Allocate memory for results to return
    wifi_scan_result_t *scan_results_ptr = malloc(sizeof(wifi_scan_result_t) * scan_results_count);
    if (scan_results_ptr == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        *results = NULL;
        return 0;
    }

    // Copy results from global array
    memcpy(scan_results_ptr, scan_results, sizeof(wifi_scan_result_t) * scan_results_count);

    *results = scan_results_ptr;
    ESP_LOGI(TAG, "Returning %d scan results to caller", scan_results_count);

    return scan_results_count;
}

/**
 * @brief Task function for testing WiFi credentials in parallel with AP mode
 */
static void credential_test_event_handler(void* arg, esp_event_base_t event_base,
                                         int32_t event_id, void* event_data)
{
    EventGroupHandle_t test_event_group = (EventGroupHandle_t)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Credential test: STA connected to AP");
        xEventGroupSetBits(test_event_group, BIT0); // TEST_CONNECTED_BIT
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Credential test: STA disconnected from AP");
        xEventGroupSetBits(test_event_group, BIT1); // TEST_FAILED_BIT
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Credential test: IP obtained: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(test_event_group, BIT2); // TEST_IP_BIT
    }
}

static void credential_test_task_func(void *pvParameters)
{
    char *ssid = ((char**)pvParameters)[0];
    char *password = ((char**)pvParameters)[1];

    ESP_LOGI(TAG, "Starting parallel credential test for SSID: %s", ssid);

    // Create a separate event group for credential testing
    EventGroupHandle_t test_event_group = xEventGroupCreate();
    const int TEST_CONNECTED_BIT = BIT0;
    const int TEST_FAILED_BIT = BIT1;
    const int TEST_IP_BIT = BIT2;

    // Register temporary event handlers for credential testing
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &credential_test_event_handler, test_event_group));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &credential_test_event_handler, test_event_group));

    // Configure STA interface for testing
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    // Set STA mode temporarily for testing
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // Start connection attempt
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start credential test connection: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Wait for connection result or timeout
    EventBits_t bits = xEventGroupWaitBits(test_event_group,
                                          TEST_CONNECTED_BIT | TEST_FAILED_BIT,
                                          pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(CREDENTIAL_TEST_TIMEOUT_MS));

    if (bits & TEST_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Credential test: Connected to AP, waiting for IP...");

        // Wait for IP assignment
        EventBits_t ip_bits = xEventGroupWaitBits(test_event_group,
                                                 TEST_IP_BIT,
                                                 pdFALSE, pdFALSE,
                                                 pdMS_TO_TICKS(10000)); // 10 seconds for IP

        if (ip_bits & TEST_IP_BIT) {
            // Connection successful - get IP address
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
                snprintf(tested_sta_ip, sizeof(tested_sta_ip), IPSTR, IP2STR(&ip_info.ip));
                ESP_LOGI(TAG, "Credential test successful - STA IP: %s", tested_sta_ip);
            } else {
                ESP_LOGE(TAG, "Failed to get IP info after successful connection");
                memset(tested_sta_ip, 0, sizeof(tested_sta_ip));
            }
        } else {
            ESP_LOGW(TAG, "Credential test: Connected but no IP assigned within timeout");
            memset(tested_sta_ip, 0, sizeof(tested_sta_ip));
        }
    } else {
        ESP_LOGW(TAG, "Credential test failed or timed out - no connection established");
        memset(tested_sta_ip, 0, sizeof(tested_sta_ip));
    }

    // Disconnect STA interface
    esp_wifi_disconnect();

cleanup:
    // Unregister temporary event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &credential_test_event_handler);
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, &credential_test_event_handler);

    // Clean up
    vEventGroupDelete(test_event_group);
    free(ssid);
    free(password);
    is_testing_credentials = false;
    credential_test_task = NULL;

    ESP_LOGI(TAG, "Credential test task completed");
    vTaskDelete(NULL);
}

/**
 * @brief Test WiFi credentials in parallel with AP mode operation
 */
bool wifi_test_credentials(const char* ssid, const char* password)
{
    if (is_testing_credentials) {
        ESP_LOGW(TAG, "Credential test already in progress");
        return false;
    }

    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID for credential testing");
        return false;
    }

    ESP_LOGI(TAG, "Starting parallel credential test for SSID: %s", ssid);

    // Clear any previous test results
    wifi_clear_tested_sta_ip();

    // Allocate memory for task parameters
    char *task_ssid = strdup(ssid);
    char *task_password = strdup(password ? password : "");

    if (!task_ssid || !task_password) {
        ESP_LOGE(TAG, "Failed to allocate memory for credential test");
        free(task_ssid);
        free(task_password);
        return false;
    }

    char *params[2] = {task_ssid, task_password};
    is_testing_credentials = true;

    // Create task for parallel testing
    BaseType_t result = xTaskCreate(&credential_test_task_func, "credential_test",
                                   4096, params, 5, &credential_test_task);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create credential test task");
        free(task_ssid);
        free(task_password);
        is_testing_credentials = false;
        return false;
    }

    ESP_LOGI(TAG, "Credential test task started successfully");
    return true;
}

/**
 * @brief Transition from APSTA mode to STA-only mode after successful credential testing
 * This should be called after the user has been redirected to the STA IP
 */
void wifi_transition_to_sta_mode(void)
{
    ESP_LOGI(TAG, "Transitioning from APSTA to STA-only mode");

    // Stop captive portal and DNS responder
    captive_portal_stop();
    dns_responder_stop();

    // Switch to STA-only mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // The STA interface should already be configured from the credential test
    // Just ensure it's started
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Successfully transitioned to STA-only mode");
}

/**
 * @brief Check if credential testing is currently in progress
 */
bool wifi_is_testing_credentials(void)
{
    return is_testing_credentials;
}

/**
 * @brief Get the STA IP address from successful credential testing
 */
const char* wifi_get_tested_sta_ip(void)
{
    return tested_sta_ip[0] != '\0' ? tested_sta_ip : NULL;
}

/**
 * @brief Clear the cached STA IP from credential testing
 */
void wifi_clear_tested_sta_ip(void)
{
    memset(tested_sta_ip, 0, sizeof(tested_sta_ip));
}