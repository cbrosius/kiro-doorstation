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
    
    // Start WiFi Manager
    wifi_manager_init();

    // Check if we're in captive portal mode (no saved WiFi config)
    wifi_manager_config_t wifi_config = wifi_load_config();
    if (!wifi_config.configured) {
        ESP_LOGI(TAG, "No WiFi configuration found - starting in CAPTIVE PORTAL mode");
        ESP_LOGI(TAG, "Captive portal will provide WiFi setup interface on port 80");
        ESP_LOGI(TAG, "DNS responder will redirect all queries to 192.168.4.1");

        // Start captive portal and DNS responder
        if (!captive_portal_start()) {
            ESP_LOGE(TAG, "Failed to start captive portal - system will not be accessible");
        }
        if (!dns_responder_start()) {
            ESP_LOGE(TAG, "Failed to start DNS responder - captive portal may not work properly");
        }

        // In captive portal mode, we don't initialize NTP, SIP, or HTTPS server
        // The device will stay in this mode until WiFi is configured
        ESP_LOGI(TAG, "Captive portal mode active - waiting for WiFi configuration");

        // Main loop for captive portal mode
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Check if WiFi is now configured (user completed setup)
            wifi_manager_config_t check_config = wifi_load_config();
            if (check_config.configured) {
                ESP_LOGI(TAG, "WiFi configuration detected - transitioning to normal mode");
                break;
            }
        }

        // Stop captive portal and DNS responder
        ESP_LOGI(TAG, "Stopping captive portal and DNS responder");
        captive_portal_stop();
        dns_responder_stop();

        // Now proceed with normal initialization
        ESP_LOGI(TAG, "Transitioning to normal operation mode");
    }

    // Wait for IP address before initializing network-dependent services
    ESP_LOGI(TAG, "Waiting for IP address before initializing NTP and SIP...");
    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
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