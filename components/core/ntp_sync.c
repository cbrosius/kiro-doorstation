#include "ntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "wifi_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "NTP";
static bool time_synced = false;
static time_t last_sync_time = 0;
static ntp_config_t current_config = {
    .server = NTP_DEFAULT_SERVER,
    .timezone = NTP_DEFAULT_TIMEZONE,
    .configured = false
};

// Timezone name to POSIX string mapping
typedef struct {
    const char* name;
    const char* posix;
} timezone_map_t;

static const timezone_map_t timezone_mappings[] = {
    // Europe
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"},
    {"Europe/Istanbul", "TRT-3"},
    
    // Americas
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6CDT,M4.1.0,M10.5.0"},
    {"America/Sao_Paulo", "BRT3BRST,M10.3.0/0,M2.3.0/0"},
    {"America/Buenos_Aires", "ART3"},
    
    // Asia
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Singapore", "SGT-8"},
    {"Asia/Seoul", "KST-9"},
    {"Asia/Bangkok", "ICT-7"},
    {"Asia/Dubai", "GST-4"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Karachi", "PKT-5"},
    {"Asia/Tehran", "IRST-3:30IRDT,J79/24,J263/24"},
    
    // Australia
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Perth", "AWST-8"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    
    // Pacific
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Fiji", "FJT-12FJST,M11.1.0,M1.3.0/3"},
    {"Pacific/Honolulu", "HST10"},
    
    // Africa
    {"Africa/Cairo", "EET-2"},
    {"Africa/Johannesburg", "SAST-2"},
    {"Africa/Lagos", "WAT-1"},
    {"Africa/Nairobi", "EAT-3"},
    
    // UTC
    {"UTC", "UTC0"},
    {"GMT", "GMT0"},
    
    {NULL, NULL}  // Terminator
};

// Convert timezone name to POSIX string
static const char* timezone_name_to_posix(const char* name)
{
    // If it already looks like a POSIX string (contains numbers or special chars), return as-is
    if (strchr(name, '-') || strchr(name, '+') || strchr(name, ',') || isdigit((unsigned char)name[0])) {
        return name;
    }
    
    // Search in mapping table
    for (int i = 0; timezone_mappings[i].name != NULL; i++) {
        if (strcasecmp(name, timezone_mappings[i].name) == 0) {
            ESP_LOGI(TAG, "Mapped timezone '%s' to '%s'", name, timezone_mappings[i].posix);
            return timezone_mappings[i].posix;
        }
    }
    
    // Not found, return as-is and let system handle it
    ESP_LOGW(TAG, "Timezone '%s' not found in mapping, using as-is", name);
    return name;
}

// Callback when time is synchronized
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized with NTP server");
    time_synced = true;
    
    // Update last sync time
    time(&last_sync_time);
    
    // Log the synchronized time
    char time_str[64];
    ntp_get_time_string(time_str, sizeof(time_str));
    ESP_LOGI(TAG, "Current time: %s", time_str);
}

void ntp_sync_init(void)
{
    ESP_LOGI(TAG, "Initializing NTP time synchronization");
    
    // Load configuration from NVS
    ntp_config_t config = ntp_load_config();
    if (config.configured) {
        strncpy(current_config.server, config.server, sizeof(current_config.server) - 1);
        strncpy(current_config.timezone, config.timezone, sizeof(current_config.timezone) - 1);
        current_config.configured = true;
        ESP_LOGI(TAG, "Loaded NTP config: server=%s, timezone=%s", 
                 current_config.server, current_config.timezone);
    } else {
        ESP_LOGI(TAG, "Using default NTP config: server=%s, timezone=%s",
                 current_config.server, current_config.timezone);
    }
    
    // Convert timezone name to POSIX if needed
    const char* posix_tz = timezone_name_to_posix(current_config.timezone);
    
    // Set timezone
    setenv("TZ", posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s (POSIX: %s)", current_config.timezone, posix_tz);
    
    // Check IP status before initializing SNTP
    ESP_LOGI(TAG, "NTP init: WiFi connected: %s", wifi_is_connected() ? "yes" : "no");

    // Initialize SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, current_config.server);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "NTP sync started with server: %s", current_config.server);
}

bool ntp_is_synced(void)
{
    return time_synced;
}

void ntp_get_time_string(char* buffer, size_t max_len)
{
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    strftime(buffer, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

uint64_t ntp_get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

void ntp_save_config(const char* server, const char* timezone)
{
    ESP_LOGI(TAG, "Saving NTP configuration");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ntp_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "server", server);
        nvs_set_str(nvs_handle, "timezone", timezone);
        nvs_set_u8(nvs_handle, "configured", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "NTP configuration saved: server=%s, timezone=%s", server, timezone);
    } else {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
    }
}

ntp_config_t ntp_load_config(void)
{
    ntp_config_t config = {
        .server = NTP_DEFAULT_SERVER,
        .timezone = NTP_DEFAULT_TIMEZONE,
        .configured = false
    };
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ntp_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        uint8_t configured = 0;
        
        nvs_get_u8(nvs_handle, "configured", &configured);
        if (configured) {
            required_size = sizeof(config.server);
            nvs_get_str(nvs_handle, "server", config.server, &required_size);
            
            required_size = sizeof(config.timezone);
            nvs_get_str(nvs_handle, "timezone", config.timezone, &required_size);
            
            config.configured = true;
        }
        nvs_close(nvs_handle);
    }
    
    return config;
}

void ntp_set_config(const char* server, const char* timezone)
{
    ESP_LOGI(TAG, "Updating NTP configuration");
    
    // Update current config
    strncpy(current_config.server, server, sizeof(current_config.server) - 1);
    current_config.server[sizeof(current_config.server) - 1] = '\0';
    
    strncpy(current_config.timezone, timezone, sizeof(current_config.timezone) - 1);
    current_config.timezone[sizeof(current_config.timezone) - 1] = '\0';
    
    current_config.configured = true;
    
    // Save to NVS
    ntp_save_config(server, timezone);
    
    // Stop current SNTP
    esp_sntp_stop();
    
    // Convert timezone name to POSIX if needed
    const char* posix_tz = timezone_name_to_posix(timezone);
    
    // Set new timezone
    setenv("TZ", posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone updated: %s (POSIX: %s)", timezone, posix_tz);
    
    // Restart SNTP with new server
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    
    time_synced = false; // Reset sync flag
    
    ESP_LOGI(TAG, "NTP configuration updated and restarted");
}

const char* ntp_get_server(void)
{
    return current_config.server;
}

const char* ntp_get_timezone(void)
{
    return current_config.timezone;
}

void ntp_force_sync(void)
{
    ESP_LOGI(TAG, "Forcing NTP time synchronization");
    esp_sntp_stop();
    esp_sntp_init();
    time_synced = false;
}

time_t ntp_get_last_sync_time(void)
{
    return last_sync_time;
}

// Custom log function with real timestamps
int ntp_log_timestamp(char* buffer, size_t buffer_len)
{
    if (time_synced) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Format: "YYYY-MM-DD HH:MM:SS"
        return snprintf(buffer, buffer_len, "%04d-%02d-%02d %02d:%02d:%02d",
                       timeinfo.tm_year + 1900,
                       timeinfo.tm_mon + 1,
                       timeinfo.tm_mday,
                       timeinfo.tm_hour,
                       timeinfo.tm_min,
                       timeinfo.tm_sec);
    }
    return 0;  // Return 0 if not synced, use default timestamp
}
