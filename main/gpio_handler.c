#include "gpio_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sip_client.h"

static const char *TAG = "GPIO";
static bool light_state = false;

static void IRAM_ATTR doorbell_isr_handler(void* arg)
{
    doorbell_t bell = (doorbell_t)(int)arg;
    // ISR - nur Flag setzen, Verarbeitung in Task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if (bell == DOORBELL_1) {
        ESP_LOGI(TAG, "Doorbell 1 pressed");
        sip_client_make_call("apartment1@example.com");
    } else if (bell == DOORBELL_2) {
        ESP_LOGI(TAG, "Doorbell 2 pressed");
        sip_client_make_call("apartment2@example.com");
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void gpio_handler_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO Handler");
    
    // Configure doorbell inputs
    gpio_config_t doorbell_config = {
        .pin_bit_mask = (1ULL << DOORBELL_1_PIN) | (1ULL << DOORBELL_2_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&doorbell_config);
    
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
    
    // Interrupt Service installieren
    gpio_install_isr_service(0);
    gpio_isr_handler_add(DOORBELL_1_PIN, doorbell_isr_handler, (void*)DOORBELL_1);
    gpio_isr_handler_add(DOORBELL_2_PIN, doorbell_isr_handler, (void*)DOORBELL_2);
    
    ESP_LOGI(TAG, "GPIO Handler initialisiert");
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