#include "hardware_status.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ntp_sync.h"
#include <stdio.h>
#include <string.h>


static const char *TAG = "HW_STATUS";

#define MAX_HW_LOGS 100

static hw_event_log_t event_logs[MAX_HW_LOGS];
static int log_head = 0;
static int log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;
static hw_stats_t global_stats = {0};
static int64_t start_time_us = 0;

void hw_status_init(void) {
  log_mutex = xSemaphoreCreateMutex();
  start_time_us = esp_timer_get_time();
  ESP_LOGI(TAG, "Hardware status monitoring initialized");
  hw_status_log_event(HW_EVENT_SYSTEM_BOOT, 0, "System Start");
}

void hw_status_log_event(hw_event_type_t type, int32_t value,
                         const char *metadata) {
  if (!log_mutex)
    return;

  if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    hw_event_log_t *entry = &event_logs[log_head];

    if (ntp_is_synced()) {
      entry->timestamp = ntp_get_timestamp_ms();
    } else {
      entry->timestamp = (uint64_t)(esp_timer_get_time() / 1000);
    }

    entry->type = type;
    entry->value = value;

    if (metadata) {
      strncpy(entry->metadata, metadata, sizeof(entry->metadata) - 1);
      entry->metadata[sizeof(entry->metadata) - 1] = '\0';
    } else {
      entry->metadata[0] = '\0';
    }

    switch (type) {
    case HW_EVENT_DOOR_OPEN:
      global_stats.door_open_count++;
      break;
    case HW_EVENT_LIGHT_TOGGLE:
      global_stats.light_toggle_count++;
      break;
    case HW_EVENT_BELL_PRESS:
      global_stats.bell_press_count++;
      break;
    default:
      break;
    }

    log_head = (log_head + 1) % MAX_HW_LOGS;
    if (log_count < MAX_HW_LOGS) {
      log_count++;
    }

    xSemaphoreGive(log_mutex);
  }
}

int hw_status_get_logs(hw_event_log_t *entries, int max_entries,
                       uint64_t since_timestamp) {
  if (!log_mutex || !entries || max_entries <= 0)
    return 0;

  int count = 0;
  if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    int start = (log_head - log_count + MAX_HW_LOGS) % MAX_HW_LOGS;

    for (int i = 0; i < log_count && count < max_entries; i++) {
      int idx = (start + i) % MAX_HW_LOGS;
      if (event_logs[idx].timestamp > since_timestamp) {
        memcpy(&entries[count], &event_logs[idx], sizeof(hw_event_log_t));
        count++;
      }
    }
    xSemaphoreGive(log_mutex);
  }
  return count;
}

void hw_status_get_stats(hw_stats_t *stats) {
  if (!stats || !log_mutex)
    return;

  if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memcpy(stats, &global_stats, sizeof(hw_stats_t));
    stats->uptime_seconds =
        (uint32_t)((esp_timer_get_time() - start_time_us) / 1000000);
    xSemaphoreGive(log_mutex);
  }
}
