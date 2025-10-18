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
    
    // NTP time synchronization (after WiFi)
    ntp_sync_init();
    
    // Ensure certificate exists (generate if needed, after all system init)
    // This is done before web server to ensure HTTPS has a certificate
    cert_ensure_exists();
    
    // Start Web Server
    web_server_start();

    // Initialize SIP Client
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