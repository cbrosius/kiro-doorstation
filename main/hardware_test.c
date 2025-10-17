#include "hardware_test.h"
#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static const char *TAG = "HW_TEST";

// Hardware test context structure with state tracking
typedef struct {
    bool door_test_active;           // Door opener test currently running
    uint32_t door_test_start_time;   // Start time in ticks
    uint32_t door_test_duration;     // Duration in milliseconds
    SemaphoreHandle_t test_mutex;    // Mutex for thread safety
    TaskHandle_t timeout_task_handle; // Handle for timeout task
} hardware_test_context_t;

static hardware_test_context_t test_ctx = {0};

// Doorbell event structure (matches gpio_handler.c)
typedef struct {
    doorbell_t bell;
} doorbell_event_t;

// Timeout task to automatically deactivate door relay
static void hardware_test_door_timeout_task(void* arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
        
        if (xSemaphoreTake(test_ctx.test_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (test_ctx.door_test_active) {
                uint32_t elapsed = (xTaskGetTickCount() - test_ctx.door_test_start_time) * portTICK_PERIOD_MS;
                
                if (elapsed >= test_ctx.door_test_duration) {
                    // Timeout reached - deactivate relay
                    gpio_set_level(DOOR_RELAY_PIN, 0);
                    test_ctx.door_test_active = false;
                    ESP_LOGI(TAG, "Door opener test completed (timeout)");
                }
            }
            xSemaphoreGive(test_ctx.test_mutex);
        }
    }
}

void hardware_test_init(void)
{
    ESP_LOGI(TAG, "Initializing Hardware Test module");
    
    // Initialize context
    test_ctx.door_test_active = false;
    test_ctx.door_test_start_time = 0;
    test_ctx.door_test_duration = 0;
    
    // Create mutex for thread safety
    test_ctx.test_mutex = xSemaphoreCreateMutex();
    if (test_ctx.test_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create test mutex");
        return;
    }
    
    // Create timeout task
    BaseType_t result = xTaskCreate(
        hardware_test_door_timeout_task,
        "hw_test_timeout",
        2048,
        NULL,
        5,
        &test_ctx.timeout_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create timeout task");
        return;
    }
    
    ESP_LOGI(TAG, "Hardware Test module initialized");
}

esp_err_t hardware_test_doorbell(doorbell_t bell)
{
    // Validate bell number
    if (bell != DOORBELL_1 && bell != DOORBELL_2) {
        ESP_LOGE(TAG, "Invalid bell number: %d (must be 1 or 2)", bell);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Testing doorbell %d", bell);
    
    // Simulate doorbell press by sending event to doorbell queue
    // This triggers the same logic as a physical button press
    doorbell_event_t event = { .bell = bell };
    
    if (doorbell_queue != NULL) {
        if (xQueueSend(doorbell_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send doorbell event to queue");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Doorbell queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Doorbell %d test triggered", bell);
    return ESP_OK;
}

esp_err_t hardware_test_door_opener(uint32_t duration_ms)
{
    // Validate duration (1000ms to 10000ms)
    if (duration_ms < 1000 || duration_ms > 10000) {
        ESP_LOGE(TAG, "Invalid duration: %lu ms (must be 1000-10000)", duration_ms);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if test already active
    if (xSemaphoreTake(test_ctx.test_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return ESP_FAIL;
    }
    
    if (test_ctx.door_test_active) {
        xSemaphoreGive(test_ctx.test_mutex);
        ESP_LOGW(TAG, "Door opener test already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Start door opener test
    test_ctx.door_test_active = true;
    test_ctx.door_test_start_time = xTaskGetTickCount();
    test_ctx.door_test_duration = duration_ms;
    
    // Activate door relay
    gpio_set_level(DOOR_RELAY_PIN, 1);
    
    xSemaphoreGive(test_ctx.test_mutex);
    
    ESP_LOGI(TAG, "Door opener test started (duration: %lu ms)", duration_ms);
    return ESP_OK;
}

esp_err_t hardware_test_light_toggle(void)
{
    ESP_LOGI(TAG, "Testing light relay toggle");
    
    // Use existing light_relay_toggle function
    light_relay_toggle();
    
    return ESP_OK;
}

void hardware_test_get_state(hardware_state_t* state)
{
    if (state == NULL) {
        return;
    }
    
    // Get door relay state
    if (xSemaphoreTake(test_ctx.test_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state->door_relay_active = test_ctx.door_test_active;
        
        if (test_ctx.door_test_active) {
            uint32_t elapsed = (xTaskGetTickCount() - test_ctx.door_test_start_time) * portTICK_PERIOD_MS;
            if (elapsed < test_ctx.door_test_duration) {
                state->door_relay_remaining_ms = test_ctx.door_test_duration - elapsed;
            } else {
                state->door_relay_remaining_ms = 0;
            }
        } else {
            state->door_relay_remaining_ms = 0;
        }
        
        xSemaphoreGive(test_ctx.test_mutex);
    } else {
        state->door_relay_active = false;
        state->door_relay_remaining_ms = 0;
    }
    
    // Get light relay state
    state->light_relay_active = (gpio_get_level(LIGHT_RELAY_PIN) == 1);
    
    // Get doorbell button states
    state->bell1_pressed = is_doorbell_pressed(DOORBELL_1);
    state->bell2_pressed = is_doorbell_pressed(DOORBELL_2);
}

esp_err_t hardware_test_stop_all(void)
{
    ESP_LOGI(TAG, "Emergency stop - deactivating all tests");
    
    if (xSemaphoreTake(test_ctx.test_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for emergency stop");
        return ESP_FAIL;
    }
    
    // Deactivate door relay immediately
    if (test_ctx.door_test_active) {
        gpio_set_level(DOOR_RELAY_PIN, 0);
        test_ctx.door_test_active = false;
        test_ctx.door_test_start_time = 0;
        test_ctx.door_test_duration = 0;
        ESP_LOGI(TAG, "Door opener test stopped");
    }
    
    xSemaphoreGive(test_ctx.test_mutex);
    
    ESP_LOGI(TAG, "All tests stopped");
    return ESP_OK;
}
