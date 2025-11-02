#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "web_server.h"
#include "sip_client.h"
#include "audio_handler.h"
#include "gpio_handler.h"
#include "hardware_test.h"
#include "dtmf_decoder.h"
#include "ntp_sync.h"
#include "cert_manager.h"
#include "auth_manager.h"
#include "captive_portal.h"
#include "dns_responder.h"

static const char *TAG = "MAIN";

/**
 * @brief Starts the captive portal and DNS responder.
 * This function is registered as a callback to be executed when the WiFi manager enters AP mode.
 */
static void start_captive_portal_services(void) {
    ESP_LOGI(TAG, "Starting captive portal services as requested by WiFi manager.");
    if (!captive_portal_start()) {
        ESP_LOGE(TAG, "Failed to start captive portal - system will not be accessible for configuration.");
    }
    if (!dns_responder_start()) {
        ESP_LOGE(TAG, "Failed to start DNS responder - captive portal may not work properly.");
    }
}

/**
 * @brief Session cleanup task - runs periodically to clean up expired sessions
 */
static void session_cleanup_task(void *pvParameters) {
    ESP_LOGI(TAG, "Session cleanup task started");

    while (1) {
        // Clean up expired sessions every 60 seconds
        vTaskDelay(pdMS_TO_TICKS(60000));
        auth_cleanup_expired_sessions();
    }
}

void app_main(void)
{

    ESP_LOGI(TAG, "ESP32 SIP Door Station started");

    // PSRAM Diagnostic
    ESP_LOGI(TAG, "PSRAM Diagnostic:");
    ESP_LOGI(TAG, "Total heap size: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Internal heap free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "SPIRAM heap free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Largest internal block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "Largest SPIRAM block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize GPIO
    gpio_handler_init();
    
    // Start password reset monitor (BOOT button)
    gpio_start_reset_monitor();
    
    // Initialize Hardware Test (after GPIO)
    hardware_test_init();
    
    // Initialize Audio
    audio_handler_init();
    
    // Initialize DTMF Decoder
    dtmf_decoder_init();
    
    // Initialize certificate manager (check only, don't generate yet)
    cert_manager_init();
    
    // Register the callback for starting captive portal services
    wifi_manager_register_ap_start_callback(start_captive_portal_services);

    // Start WiFi Manager. It will either connect to a saved network or start AP mode and trigger the callback.
    wifi_manager_init();

    // Check current WiFi mode
    wifi_mode_t current_wifi_mode;
    esp_wifi_get_mode(&current_wifi_mode);

    if (current_wifi_mode == WIFI_MODE_APSTA) {
        // In APSTA mode, captive portal services are already started by the WiFi manager callback
        ESP_LOGI(TAG, "Device is in APSTA mode - captive portal already started by WiFi manager");

        // In APSTA mode, wait for credential testing to complete and produce a redirect
        ESP_LOGI(TAG, "In APSTA mode - waiting for credential testing to complete and redirect user");

        // Wait for credential test to complete (tested STA IP available)
        while (wifi_get_tested_sta_ip() == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if (wifi_get_tested_sta_ip() != NULL) {
            ESP_LOGI(TAG, "Credential testing produced STA IP - waiting for user redirect to complete");
            // The captive portal will handle the redirect and mode transition when user accesses STA IP
            // Wait for the mode transition to happen
            wifi_mode_t current_mode;
            esp_wifi_get_mode(&current_mode);
            while (current_mode == WIFI_MODE_APSTA) {
                esp_wifi_get_mode(&current_mode);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            ESP_LOGI(TAG, "Mode transitioned to STA-only, proceeding with normal initialization");
        } else {
            ESP_LOGI(TAG, "Credential testing failed - staying in APSTA mode for user to try again");
        }
        // The captive portal will handle credential testing and mode transitions
    } else if (current_wifi_mode == WIFI_MODE_STA) {
        // In STA mode, check if we have saved WiFi config and connect
        wifi_manager_config_t saved_config = wifi_load_config();
        if (saved_config.configured) {
            ESP_LOGI(TAG, "Device is in STA mode with saved config - connecting to: %s", saved_config.ssid);
            wifi_connect_sta(saved_config.ssid, saved_config.password);
        } else {
            ESP_LOGI(TAG, "Device is in STA mode but no saved config - this shouldn't happen");
        }
    }

    // Wait for IP address before initializing network-dependent services
    ESP_LOGI(TAG, "Waiting for IP address before initializing NTP and SIP...");
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "IP address obtained, proceeding with NTP and SIP initialization");

    // NTP time synchronization (after WiFi and IP)
    ntp_sync_init();

    // Ensure certificate exists (generate if needed, after all system init)
    // This is done before web server to ensure HTTPS has a certificate
    cert_ensure_exists();

    // Start Web Server
    web_server_start();

    // Initialize SIP Client (after IP is available)
    sip_client_init();

    // Initialize authentication manager (for session cleanup)
    auth_manager_init();
    
    ESP_LOGI(TAG, "All components initialized");

    // Final PSRAM Diagnostic after initialization
    ESP_LOGI(TAG, "Post-init PSRAM Diagnostic:");
    ESP_LOGI(TAG, "Total heap size: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Internal heap free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "SPIRAM heap free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Largest internal block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "Largest SPIRAM block: %d bytes", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));



    // Start session cleanup task
    xTaskCreate(&session_cleanup_task, "session_cleanup", 2048, NULL, 5, NULL);

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}