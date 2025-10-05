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
        ESP_LOGI(TAG, "WiFi STA started, attempting to connect");
        esp_wifi_connect();
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
    }
}

char* wifi_scan_networks(void) {
    ESP_LOGI(TAG, "Scanning for WiFi networks - using APSTA mode");

    // Switch to APSTA mode to allow scanning while keeping AP active
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Reconfigure AP in APSTA mode
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32-Doorbell",
            .ssid_len = strlen("ESP32-Doorbell"),
            .password = "doorbell123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    wifi_scan_config_t scan_config = {0};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        // Switch back to AP only
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        return strdup("[]");
    }

    vTaskDelay(pdMS_TO_TICKS(3000)); // Wait for scan to complete

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Scan completed, found %d APs", ap_count);

    char *json = NULL;
    if (ap_count > 0) {
        wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_list != NULL) {
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

            // Build JSON array
            json = malloc(4096); // Should be enough
            if (json != NULL) {
                strcpy(json, "[");
                for (int i = 0; i < ap_count; i++) {
                    char ssid_escaped[33];
                    // Simple escape for JSON
                    char *p = ssid_escaped;
                    for (int j = 0; j < 32 && ap_list[i].ssid[j]; j++) {
                        if (ap_list[i].ssid[j] == '"' || ap_list[i].ssid[j] == '\\') {
                            *p++ = '\\';
                        }
                        *p++ = ap_list[i].ssid[j];
                    }
                    *p = 0;

                    char entry[128];
                    snprintf(entry, sizeof(entry), "\"%s\"", ssid_escaped);
                    strcat(json, entry);
                    if (i < ap_count - 1) {
                        strcat(json, ",");
                    }
                }
                strcat(json, "]");
            }
            free(ap_list);
        }
    }

    if (json == NULL) {
        json = strdup("[]");
    }

    // Switch back to AP only mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    ESP_LOGI(TAG, "Switched back to AP mode");
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
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32-Doorbell",
            .ssid_len = strlen("ESP32-Doorbell"),
            .password = "doorbell123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
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