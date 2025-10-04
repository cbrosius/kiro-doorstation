#include "dtmf_decoder.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "DTMF";
static dtmf_callback_t dtmf_callback = NULL;

// DTMF Zeichen-Matrix
static const char dtmf_chars[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

static void dtmf_command_handler(dtmf_tone_t tone)
{
    static char command_buffer[10] = {0};
    static int command_index = 0;
    
    ESP_LOGI(TAG, "DTMF Ton empfangen: %c", tone);
    
    if (tone == DTMF_HASH) {
        // Kommando ausführen
        if (strcmp(command_buffer, "*1") == 0) {
            ESP_LOGI(TAG, "Türöffner aktivieren");
            xTaskCreate((TaskFunction_t)door_relay_activate, "door_task", 2048, NULL, 5, NULL);
        } else if (strcmp(command_buffer, "*2") == 0) {
            ESP_LOGI(TAG, "Licht umschalten");
            light_relay_toggle();
        }
        
        // Buffer zurücksetzen
        memset(command_buffer, 0, sizeof(command_buffer));
        command_index = 0;
    } else if (command_index < sizeof(command_buffer) - 1) {
        command_buffer[command_index++] = tone;
    }
}

void dtmf_decoder_init(void)
{
    ESP_LOGI(TAG, "DTMF Decoder initialisieren");
    dtmf_callback = dtmf_command_handler;
    ESP_LOGI(TAG, "DTMF Decoder initialisiert");
}

void dtmf_set_callback(dtmf_callback_t callback)
{
    dtmf_callback = callback;
}

dtmf_tone_t dtmf_decode_buffer(const int16_t *buffer, size_t length)
{
    // Vereinfachte DTMF-Erkennung über Goertzel-Algorithmus
    // In einer echten Implementierung würde hier eine FFT oder 
    // Goertzel-Filter für jede DTMF-Frequenz verwendet
    
    float max_magnitude = 0;
    int detected_low = -1, detected_high = -1;
    
    // Hier würde die eigentliche Frequenzanalyse stattfinden
    // Für das Beispiel nehmen wir eine vereinfachte Erkennung
    
    // Schwellwert für DTMF-Erkennung
    const float threshold = 1000.0f;
    
    if (max_magnitude > threshold) {
        if (detected_low >= 0 && detected_low < 4 && 
            detected_high >= 0 && detected_high < 4) {
            return dtmf_chars[detected_low][detected_high];
        }
    }
    
    return DTMF_NONE;
}

void dtmf_process_audio(const int16_t *buffer, size_t length)
{
    dtmf_tone_t tone = dtmf_decode_buffer(buffer, length);
    if (tone != DTMF_NONE && dtmf_callback) {
        dtmf_callback(tone);
    }
}