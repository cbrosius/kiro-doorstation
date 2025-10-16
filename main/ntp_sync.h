#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <stdbool.h>
#include <time.h>

#define NTP_SERVER_MAX_LEN 64
#define NTP_DEFAULT_SERVER "pool.ntp.org"
#define NTP_TIMEZONE_MAX_LEN 64
#define NTP_DEFAULT_TIMEZONE "UTC0"

typedef struct {
    char server[NTP_SERVER_MAX_LEN];
    char timezone[NTP_TIMEZONE_MAX_LEN];
    bool configured;
} ntp_config_t;

/**
 * Initialize NTP time synchronization
 * Should be called after WiFi is connected
 */
void ntp_sync_init(void);

/**
 * Check if time has been synchronized
 */
bool ntp_is_synced(void);

/**
 * Get current time as formatted string
 * Format: "YYYY-MM-DD HH:MM:SS"
 */
void ntp_get_time_string(char* buffer, size_t max_len);

/**
 * Get current timestamp in milliseconds since epoch
 */
uint64_t ntp_get_timestamp_ms(void);

/**
 * Save NTP configuration to NVS
 */
void ntp_save_config(const char* server, const char* timezone);

/**
 * Load NTP configuration from NVS
 */
ntp_config_t ntp_load_config(void);

/**
 * Set NTP server and timezone
 */
void ntp_set_config(const char* server, const char* timezone);

/**
 * Get current NTP server
 */
const char* ntp_get_server(void);

/**
 * Get current timezone
 */
const char* ntp_get_timezone(void);

/**
 * Force time synchronization
 */
void ntp_force_sync(void);

/**
 * Get timestamp for logging (returns 0 if not synced)
 * Format: "YYYY-MM-DD HH:MM:SS"
 */
int ntp_log_timestamp(char* buffer, size_t buffer_len);

#endif // NTP_SYNC_H
