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
#include "dtmf_decoder.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 SIP Door Station started");
    
    // NVS initialisieren
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // GPIO initialisieren
    gpio_handler_init();
    
    // Audio initialisieren
    audio_handler_init();
    
    // DTMF Decoder initialisieren
    dtmf_decoder_init();
    
    // WiFi Manager starten
    wifi_manager_init();
    
    // Web Server starten
    web_server_start();
    
    // SIP Client initialisieren
    sip_client_init();
    
    ESP_LOGI(TAG, "All components initialized");
    
    // Hauptschleife
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}