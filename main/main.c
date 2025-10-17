#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "web_server.h"
#include "sip_client.h"
#include "audio_handler.h"
#include "gpio_handler.h"
#include "hardware_test.h"
#include "dtmf_decoder.h"
#include "ntp_sync.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SIP Door Station started");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize GPIO
    gpio_handler_init();
    
    // Initialize Hardware Test (after GPIO)
    hardware_test_init();
    
    // Initialize Audio
    audio_handler_init();
    
    // Initialize DTMF Decoder
    dtmf_decoder_init();
    
    // Start WiFi Manager
    wifi_manager_init();
    
    // NTP time synchronization (after WiFi)
    ntp_sync_init();
    
    // Start Web Server
    web_server_start();
    
    // Initialize SIP Client
    sip_client_init();
    
    ESP_LOGI(TAG, "All components initialized");
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}