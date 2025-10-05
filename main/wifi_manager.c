#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool is_connected = false;
static int retry_count = 0;
static const int MAX_RETRIES = 10;
static bool is_scanning = false;

// ESPHome-style scan results storage
static wifi_scan_result_t scan_results[MAX_SCAN_RESULTS];
static int scan_results_count = 0;
static bool scan_results_valid = false;

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
        if (retry_count >= MAX_RETRIES) {
            ESP_LOGW(TAG, "Max retries reached, falling back to AP mode");
            wifi_start_ap_mode();
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
                        scan_results[scan_results_count].locked = (ap_list[i].authmode != WIFI_AUTH_OPEN);
                        scan_results_count++;

                        ESP_LOGI(TAG, "Stored network: %s (RSSI: %d, Locked: %s)",
                                scan_results[scan_results_count - 1].ssid,
                                scan_results[scan_results_count - 1].rssi,
                                scan_results[scan_results_count - 1].locked ? "yes" : "no");
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

char* wifi_scan_networks(void) {
    ESP_LOGI(TAG, "ESPHome-style: Checking for stored scan results");

    // ESPHome approach: Serve existing results immediately, no waiting
    // Background scanning happens independently

    char *json = malloc(4096);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON");
        return strdup("[]");
    }

    // If no results available, return empty array immediately
    // Background scan will populate results for next request
    if (!scan_results_valid || scan_results_count == 0) {
        ESP_LOGI(TAG, "No scan results available yet, returning empty array");
        strcpy(json, "[]");

        // Trigger background scan for next time, but don't wait
        if (!is_scanning) {
            ESP_LOGI(TAG, "Triggering background scan for future requests");
            wifi_start_background_scan();
        }
    } else {
        // Build JSON from stored results (ESPHome pattern)
        strcpy(json, "[");
        for (int i = 0; i < scan_results_count; i++) {
            if (i > 0) {
                strcat(json, ",");
            }

            // Escape SSID for JSON
            char ssid_escaped[66];
            char *p = ssid_escaped;
            for (int j = 0; j < 32 && scan_results[i].ssid[j]; j++) {
                if (scan_results[i].ssid[j] == '"' || scan_results[i].ssid[j] == '\\') {
                    *p++ = '\\';
                }
                *p++ = scan_results[i].ssid[j];
            }
            *p = 0;

            strcat(json, "\"");
            strcat(json, ssid_escaped);
            strcat(json, "\"");
        }
        strcat(json, "]");

        ESP_LOGI(TAG, "ESPHome-style: Serving %d stored scan results", scan_results_count);
    }

    return json;
}

void wifi_manager_init(void)
{
    ESP_LOGI(TAG, "WiFi Manager initialisieren");
    
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Gespeicherte WiFi-Konfiguration laden
    wifi_manager_config_t saved_config = wifi_load_config();

    if (saved_config.configured) {
        ESP_LOGI(TAG, "Gespeicherte WiFi-Konfiguration gefunden, verbinde mit %s", saved_config.ssid);
        wifi_connect_sta(saved_config.ssid, saved_config.password);
    } else {
        ESP_LOGI(TAG, "Keine WiFi-Konfiguration gefunden, starte AP-Modus");
        wifi_start_ap_mode();
    }
    
    ESP_LOGI(TAG, "WiFi Manager initialisiert");
}

bool wifi_is_connected(void)
{
    return is_connected;
}

void wifi_start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starte AP-Modus");

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

    ESP_LOGI(TAG, "AP gestartet. SSID: %s", wifi_config.ap.ssid);
}

void wifi_connect_sta(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Verbinde mit WiFi: %s", ssid);

    retry_count = 0; // Reset retry count when starting new connection

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_save_config(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "Speichere WiFi-Konfiguration");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        nvs_set_str(nvs_handle, "password", password);
        nvs_set_u8(nvs_handle, "configured", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi-Konfiguration gespeichert");
    } else {
        ESP_LOGE(TAG, "Fehler beim Öffnen des NVS: %s", esp_err_to_name(err));
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