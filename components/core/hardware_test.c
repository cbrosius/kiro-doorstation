#include "hardware_test.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gpio_handler.h"
#include "hardware_status.h"


static const char *TAG = "HW_TEST";

// Doorbell event structure (matches gpio_handler.c)
typedef struct {
  doorbell_t bell;
} doorbell_event_t;

void hardware_test_init(void) {
  ESP_LOGI(TAG, "Initializing Hardware Test module");

  // Ensure light relay GPIO starts in OFF state (LOW)
  gpio_set_level(LIGHT_RELAY_PIN, 0);

  ESP_LOGI(TAG, "Hardware Test module initialized");
}

esp_err_t hardware_test_doorbell(doorbell_t bell) {
  // Validate bell number
  if (bell != DOORBELL_1 && bell != DOORBELL_2) {
    ESP_LOGE(TAG, "Invalid bell number: %d (must be 1 or 2)", bell);
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Testing doorbell %d", bell);

  // Simulate doorbell press by sending event to doorbell queue
  // This triggers the same logic as a physical button press
  doorbell_event_t event = {.bell = bell};

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

esp_err_t hardware_test_door_opener(uint32_t duration_ms) {
  // Validate duration (1000ms to 10000ms)
  if (duration_ms < 1000 || duration_ms > 10000) {
    ESP_LOGE(TAG, "Invalid duration: %lu ms (must be 1000-10000)",
             (unsigned long)duration_ms);
    return ESP_ERR_INVALID_ARG;
  }

  return door_relay_activate_for_ms(duration_ms, "Web Test");
}

esp_err_t hardware_test_light_toggle(bool *new_state) {
  ESP_LOGI(TAG, "Testing light relay toggle");

  // Use the gpio_handler's toggle function to maintain consistent state
  bool toggled_state = light_relay_toggle();

  // Return new state to caller
  if (new_state != NULL) {
    *new_state = toggled_state;
  }

  ESP_LOGI(TAG, "Light relay toggled: %s", toggled_state ? "ON" : "OFF");

  return ESP_OK;
}

void hardware_test_get_state(hardware_state_t *state) {
  if (state == NULL) {
    return;
  }

  // Get unified door relay state from gpio_handler
  state->door_relay_active = door_relay_is_active();
  state->door_relay_remaining_ms = door_relay_get_remaining_ms();

  // Get light relay state
  state->light_relay_active = (gpio_get_level(LIGHT_RELAY_PIN) == 1);

  // Get doorbell button states
  state->bell1_pressed = is_doorbell_pressed(DOORBELL_1);
  state->bell2_pressed = is_doorbell_pressed(DOORBELL_2);
}

esp_err_t hardware_test_stop_all(void) {
  ESP_LOGI(TAG, "Emergency stop - deactivating all tests");

  if (door_relay_is_active()) {
    door_relay_deactivate();
  }

  ESP_LOGI(TAG, "All tests stopped");
  return ESP_OK;
}
