#ifndef NTP_LOG_H
#define NTP_LOG_H

#include "esp_log.h"
#include "ntp_sync.h"
#include <stdio.h>

// Custom log macros that prepend real timestamp when NTP is synced
// Falls back to standard ESP_LOG when not synced

#define NTP_LOGE(tag, format, ...) do { \
    char time_buf[32]; \
    if (ntp_log_timestamp(time_buf, sizeof(time_buf)) > 0) { \
        ESP_LOGE(tag, "[%s] " format, time_buf, ##__VA_ARGS__); \
    } else { \
        ESP_LOGE(tag, format, ##__VA_ARGS__); \
    } \
} while(0)

#define NTP_LOGW(tag, format, ...) do { \
    char time_buf[32]; \
    if (ntp_log_timestamp(time_buf, sizeof(time_buf)) > 0) { \
        ESP_LOGW(tag, "[%s] " format, time_buf, ##__VA_ARGS__); \
    } else { \
        ESP_LOGW(tag, format, ##__VA_ARGS__); \
    } \
} while(0)

#define NTP_LOGI(tag, format, ...) do { \
    char time_buf[32]; \
    if (ntp_log_timestamp(time_buf, sizeof(time_buf)) > 0) { \
        ESP_LOGI(tag, "[%s] " format, time_buf, ##__VA_ARGS__); \
    } else { \
        ESP_LOGI(tag, format, ##__VA_ARGS__); \
    } \
} while(0)

#define NTP_LOGD(tag, format, ...) do { \
    char time_buf[32]; \
    if (ntp_log_timestamp(time_buf, sizeof(time_buf)) > 0) { \
        ESP_LOGD(tag, "[%s] " format, time_buf, ##__VA_ARGS__); \
    } else { \
        ESP_LOGD(tag, format, ##__VA_ARGS__); \
    } \
} while(0)

#define NTP_LOGV(tag, format, ...) do { \
    char time_buf[32]; \
    if (ntp_log_timestamp(time_buf, sizeof(time_buf)) > 0) { \
        ESP_LOGV(tag, "[%s] " format, time_buf, ##__VA_ARGS__); \
    } else { \
        ESP_LOGV(tag, format, ##__VA_ARGS__); \
    } \
} while(0)

#endif // NTP_LOG_H
