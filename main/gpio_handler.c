#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sip_client.h"

static const char *TAG = "GPIO";
static bool light_state = false;
static QueueHandle_t doorbell_queue = NULL;
static TaskHandle_t doorbell_task_handle = NULL;

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

static void IRAM_ATTR boot_button_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Send DOORBELL_1 event to simulate doorbell press
    doorbell_event_t event = { .bell = DOORBELL_1 };
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
    
    // Configure BOOT button (GPIO 0) for testing
    gpio_config_t boot_button_config = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Trigger on falling edge (button press)
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
    gpio_isr_handler_add(BOOT_BUTTON_PIN, boot_button_isr_handler, NULL);
    
    ESP_LOGI(TAG, "GPIO Handler initialized (BOOT button on GPIO 0 enabled for testing)");
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