#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sip_client.h"
#include "auth_manager.h"

static const char *TAG = "GPIO";
static bool light_state = false;
QueueHandle_t doorbell_queue = NULL;  // Non-static for hardware test access
static TaskHandle_t doorbell_task_handle = NULL;
static TaskHandle_t reset_monitor_task_handle = NULL;

// Reset button monitoring
#define RESET_BUTTON_HOLD_TIME_MS 10000  // 10 seconds

typedef struct {
    doorbell_t bell;
} doorbell_event_t;

// Task to handle doorbell events (not in ISR context)
static void doorbell_task(void *arg)
{
    doorbell_event_t event;
    static TickType_t last_call_time[2] = {0, 0}; // Debounce tracking for each bell
    const TickType_t debounce_delay = pdMS_TO_TICKS(2000); // 2 second debounce
    
    while (1) {
        if (xQueueReceive(doorbell_queue, &event, portMAX_DELAY)) {
            TickType_t current_time = xTaskGetTickCount();
            int bell_index = (event.bell == DOORBELL_1) ? 0 : 1;
            
            // Check debounce - ignore if pressed too soon after last press
            if ((current_time - last_call_time[bell_index]) < debounce_delay) {
                ESP_LOGW(TAG, "Doorbell %d press ignored (debounce)", event.bell);
                continue;
            }
            
            last_call_time[bell_index] = current_time;
            
            if (event.bell == DOORBELL_1) {
                ESP_LOGI(TAG, "Doorbell 1 pressed");
                const char* target1 = sip_get_target1();
                if (target1 && strlen(target1) > 0) {
                    sip_client_make_call(target1);
                } else {
                    ESP_LOGW(TAG, "SIP-Target1 not configured");
                }
            } else if (event.bell == DOORBELL_2) {
                ESP_LOGI(TAG, "Doorbell 2 pressed");
                const char* target2 = sip_get_target2();
                if (target2 && strlen(target2) > 0) {
                    sip_client_make_call(target2);
                } else {
                    ESP_LOGW(TAG, "SIP-Target2 not configured");
                }
            }
        }
    }
}

static void IRAM_ATTR doorbell_isr_handler(void* arg)
{
    doorbell_t bell = (doorbell_t)(int)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Send event to queue for processing in task context
    doorbell_event_t event = { .bell = bell };
    xQueueSendFromISR(doorbell_queue, &event, &xHigherPriorityTaskWoken);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}



void gpio_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO Handler");
    
    // Create queue for doorbell events
    doorbell_queue = xQueueCreate(10, sizeof(doorbell_event_t));
    if (doorbell_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create doorbell queue");
        return;
    }
    
    // Create task to handle doorbell events (increased stack for SIP operations)
    xTaskCreate(doorbell_task, "doorbell_task", 8192, NULL, 5, &doorbell_task_handle);
    
    // Configure doorbell inputs
    gpio_config_t doorbell_config = {
        .pin_bit_mask = (1ULL << DOORBELL_1_PIN) | (1ULL << DOORBELL_2_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&doorbell_config);
    
    // Configure BOOT button (GPIO 0) for password reset monitoring
    gpio_config_t boot_button_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE  // No interrupt, will be polled by reset monitor task
    };
    gpio_config(&boot_button_config);
    
    // Configure relay outputs
    gpio_config_t relay_config = {
        .pin_bit_mask = (1ULL << DOOR_RELAY_PIN) | (1ULL << LIGHT_RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&relay_config);
    
    // Turn off relays initially
    gpio_set_level(DOOR_RELAY_PIN, 0);
    gpio_set_level(LIGHT_RELAY_PIN, 0);
    
    // Install interrupt service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(DOORBELL_1_PIN, doorbell_isr_handler, (void*)DOORBELL_1);
    gpio_isr_handler_add(DOORBELL_2_PIN, doorbell_isr_handler, (void*)DOORBELL_2);
    
    ESP_LOGI(TAG, "GPIO Handler initialized (BOOT button on GPIO 0 configured for password reset)");
}

void door_relay_activate(void)
{
    ESP_LOGI(TAG, "Door opener activated");
    gpio_set_level(DOOR_RELAY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(3000)); // Active for 3 seconds
    gpio_set_level(DOOR_RELAY_PIN, 0);
    ESP_LOGI(TAG, "Door opener deactivated");
}

void light_relay_toggle(void)
{
    light_state = !light_state;
    gpio_set_level(LIGHT_RELAY_PIN, light_state ? 1 : 0);
    ESP_LOGI(TAG, "Light %s", light_state ? "on" : "off");
}

bool is_doorbell_pressed(doorbell_t bell)
{
    gpio_num_t pin = (bell == DOORBELL_1) ? DOORBELL_1_PIN : DOORBELL_2_PIN;
    return gpio_get_level(pin) == 0; // Active low
}

/**
 * @brief Task to monitor BOOT button for password reset and doorbell simulation
 * 
 * This task monitors the BOOT button (GPIO 0):
 * - Short press (< 1 second): Triggers doorbell call (for testing)
 * - Long press (10 seconds): Triggers password reset
 */
static void reset_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Reset monitor task started");
    
    const TickType_t check_interval = pdMS_TO_TICKS(100);  // Check every 100ms
    const uint32_t required_presses = RESET_BUTTON_HOLD_TIME_MS / 100;  // 100 checks for 10 seconds
    const uint32_t short_press_threshold = 10;  // 10 checks = 1 second
    uint32_t press_count = 0;
    bool was_pressed = false;
    
    while (1) {
        // Check if BOOT button is pressed (active low)
        bool is_pressed = (gpio_get_level(BOOT_BUTTON_PIN) == 0);
        
        if (is_pressed) {
            if (!was_pressed) {
                // Button just pressed
                press_count = 0;
            }
            
            press_count++;
            
            // Log progress every second (after 1 second)
            if (press_count >= short_press_threshold && press_count % 10 == 0) {
                uint32_t seconds_held = press_count / 10;
                ESP_LOGI(TAG, "BOOT button held for %lu seconds (hold 10s to reset password)...", seconds_held);
            }
            
            // Check if held long enough for password reset
            if (press_count >= required_presses) {
                ESP_LOGW(TAG, "BOOT button held for 10 seconds - deleting password!");
                
                // Reset password (deletes from NVS)
                esp_err_t err = auth_reset_password();
                if (err == ESP_OK) {
                    ESP_LOGW(TAG, "Password deleted successfully");
                    ESP_LOGW(TAG, "Initial setup wizard will be triggered on next web access");
                } else {
                    ESP_LOGE(TAG, "Password reset failed: %s", esp_err_to_name(err));
                }
                
                // Wait for button release to avoid multiple resets
                while (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                
                press_count = 0;
                ESP_LOGI(TAG, "BOOT button released");
            }
        } else {
            // Button released
            if (was_pressed && press_count > 0 && press_count < short_press_threshold) {
                // Short press detected - trigger doorbell call
                ESP_LOGI(TAG, "BOOT button short press - triggering doorbell call");
                const char* target1 = sip_get_target1();
                if (target1 && strlen(target1) > 0) {
                    sip_client_make_call(target1);
                } else {
                    ESP_LOGW(TAG, "SIP-Target1 not configured");
                }
            }
            press_count = 0;
        }
        
        was_pressed = is_pressed;
        vTaskDelay(check_interval);
    }
}

void gpio_start_reset_monitor(void)
{
    if (reset_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Reset monitor task already running");
        return;
    }
    
    // Create task to monitor reset button
    BaseType_t result = xTaskCreate(reset_monitor_task, 
                                     "reset_monitor", 
                                     8192,  // Increased stack for SIP operations
                                     NULL, 
                                     5, 
                                     &reset_monitor_task_handle);
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Reset monitor task created - short press for doorbell, hold 10s to reset password");
    } else {
        ESP_LOGE(TAG, "Failed to create reset monitor task");
    }
}