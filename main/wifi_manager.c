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

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi Verbindung getrennt, versuche Reconnect");
        is_connected = false;
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP erhalten:" IPSTR, IP2STR(&event->ip_info.ip));
        is_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
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
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
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