#ifndef HARDWARE_STATUS_H
#define HARDWARE_STATUS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>


/**
 * Hardware event types for logging
 */
typedef enum {
  HW_EVENT_DOOR_OPEN,    // Door opener activated
  HW_EVENT_LIGHT_TOGGLE, // Light relay toggled
  HW_EVENT_BELL_PRESS,   // Doorbell button pressed
  HW_EVENT_SYSTEM_BOOT,  // System started
  HW_EVENT_RESET_PRESS   // Reset button pressed (short)
} hw_event_type_t;

/**
 * Hardware event log structure
 */
typedef struct {
  uint64_t timestamp;   // NTP timestamp in ms (or uptime if not synced)
  hw_event_type_t type; // Type of event
  int32_t value;        // Event-specific value (e.g. duration, state)
  char metadata[32];    // Optional metadata (e.g. source of trigger)
} hw_event_log_t;

/**
 * Total event counts (for statistics)
 */
typedef struct {
  uint32_t door_open_count;
  uint32_t light_toggle_count;
  uint32_t bell_press_count;
  uint32_t uptime_seconds;
} hw_stats_t;

/**
 * Initialize hardware status monitoring
 */
void hw_status_init(void);

/**
 * Log a hardware event
 *
 * @param type Event type
 * @param value Event value
 * @param metadata Optional metadata string (can be NULL)
 */
void hw_status_log_event(hw_event_type_t type, int32_t value,
                         const char *metadata);

/**
 * Retrieve recent hardware logs
 *
 * @param entries Array to fill with log entries
 * @param max_entries Maximum number of entries to retrieve
 * @param since_timestamp Retrieve entries after this timestamp (0 for all)
 * @return Number of entries retrieved
 */
int hw_status_get_logs(hw_event_log_t *entries, int max_entries,
                       uint64_t since_timestamp);

/**
 * Get current stats
 *
 * @param stats Pointer to structure to fill
 */
void hw_status_get_stats(hw_stats_t *stats);

#endif // HARDWARE_STATUS_H
