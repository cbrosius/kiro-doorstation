#include "audio_handler.h"
#include "gpio_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO";

void audio_handler_init(void)
{
    ESP_LOGI(TAG, "Audio Handler initialisieren");
    
    // I2S Konfiguration für Ausgabe (Lautsprecher)
    i2s_config_t i2s_config_out = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = BITS_PER_SAMPLE,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    
    // I2S Pin-Konfiguration für Ausgabe
    i2s_pin_config_t pin_config_out = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_SD_OUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    // I2S Port 0 für Ausgabe installieren
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config_out));
    
    // I2S Konfiguration für Eingabe (Mikrofon)
    i2s_config_t i2s_config_in = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = BITS_PER_SAMPLE,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_COUNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false
    };
    
    // I2S Pin-Konfiguration für Eingabe
    i2s_pin_config_t pin_config_in = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_IN_PIN
    };
    
    // I2S Port 1 für Eingabe installieren
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_1, &pin_config_in));
    
    ESP_LOGI(TAG, "Audio Handler initialisiert");
}

void audio_start_recording(void)
{
    ESP_LOGI(TAG, "Audio-Aufnahme gestartet");
    i2s_start(I2S_NUM_1);
}

void audio_stop_recording(void)
{
    ESP_LOGI(TAG, "Audio-Aufnahme gestoppt");
    i2s_stop(I2S_NUM_1);
}

void audio_start_playback(void)
{
    ESP_LOGI(TAG, "Audio-Wiedergabe gestartet");
    i2s_start(I2S_NUM_0);
}

void audio_stop_playback(void)
{
    ESP_LOGI(TAG, "Audio-Wiedergabe gestoppt");
    i2s_stop(I2S_NUM_0);
}

size_t audio_read(int16_t *buffer, size_t length)
{
    size_t bytes_read = 0;
    esp_err_t ret = i2s_read(I2S_NUM_1, buffer, length * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
        return 0;
    }
    return bytes_read / sizeof(int16_t);
}

size_t audio_write(const int16_t *buffer, size_t length)
{
    size_t bytes_written = 0;
    esp_err_t ret = i2s_write(I2S_NUM_0, buffer, length * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
        return 0;
    }
    return bytes_written / sizeof(int16_t);
}