#include "dtmf_decoder.h"
#include "gpio_handler.h"
#include "ntp_sync.h"
#include "rtp_handler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "DTMF";
static dtmf_callback_t dtmf_callback = NULL;

// NVS namespace for security configuration
#define NVS_NAMESPACE "dtmf_security"

// Security log buffer configuration
#define SECURITY_LOG_SIZE 50

// Circular buffer for security logs
static dtmf_security_log_t security_log_buffer[SECURITY_LOG_SIZE];
static int security_log_head = 0;
static int security_log_count = 0;
static SemaphoreHandle_t security_log_mutex = NULL;

// Security configuration and state
static dtmf_security_config_t security_config = {
    .pin_enabled = false,
    .pin_code = "",
    .timeout_ms = 10000,  // Default 10 seconds
    .max_attempts = 3
};

static dtmf_command_state_t command_state = {
    .buffer = {0},
    .buffer_index = 0,
    .start_time_ms = 0,
    .failed_attempts = 0,
    .rate_limited = false,
    .last_event_ts = 0
};

// Forward declarations
static void dtmf_add_security_log(dtmf_command_type_t type, bool success, 
                                   const char* command, const char* caller_id, 
                                   const char* reason);

// Load security configuration from NVS
void dtmf_load_security_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        return;
    }

    // Load pin_enabled
    uint8_t pin_enabled = 0;
    err = nvs_get_u8(nvs_handle, "pin_enabled", &pin_enabled);
    if (err == ESP_OK) {
        security_config.pin_enabled = (pin_enabled != 0);
    }

    // Load pin_code
    size_t pin_len = sizeof(security_config.pin_code);
    err = nvs_get_str(nvs_handle, "pin_code", security_config.pin_code, &pin_len);
    if (err != ESP_OK) {
        security_config.pin_code[0] = '\0';
    }

    // Load timeout_ms
    err = nvs_get_u32(nvs_handle, "timeout_ms", &security_config.timeout_ms);
    if (err != ESP_OK) {
        security_config.timeout_ms = 10000;  // Default 10 seconds
    }

    // Load max_attempts
    err = nvs_get_u8(nvs_handle, "max_attempts", &security_config.max_attempts);
    if (err != ESP_OK) {
        security_config.max_attempts = 3;  // Default 3 attempts
    }

    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Security config loaded: PIN %s, timeout %lu ms, max attempts %d",
             security_config.pin_enabled ? "enabled" : "disabled",
             security_config.timeout_ms,
             security_config.max_attempts);
}

// Save security configuration to NVS
void dtmf_save_security_config(const dtmf_security_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return;
    }

    // Validate PIN code (digits only, 1-8 characters)
    if (config->pin_enabled) {
        size_t pin_len = strlen(config->pin_code);
        if (pin_len < 1 || pin_len > 8) {
            ESP_LOGE(TAG, "Invalid PIN length: %d (must be 1-8)", pin_len);
            return;
        }
        for (size_t i = 0; i < pin_len; i++) {
            if (config->pin_code[i] < '0' || config->pin_code[i] > '9') {
                ESP_LOGE(TAG, "Invalid PIN character: %c (must be digits only)", config->pin_code[i]);
                return;
            }
        }
    }

    // Validate timeout range (5000-30000 ms)
    if (config->timeout_ms < 5000 || config->timeout_ms > 30000) {
        ESP_LOGE(TAG, "Invalid timeout: %lu ms (must be 5000-30000)", config->timeout_ms);
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    // Save pin_enabled
    err = nvs_set_u8(nvs_handle, "pin_enabled", config->pin_enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pin_enabled: %s", esp_err_to_name(err));
    }

    // Save pin_code
    err = nvs_set_str(nvs_handle, "pin_code", config->pin_code);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pin_code: %s", esp_err_to_name(err));
    }

    // Save timeout_ms
    err = nvs_set_u32(nvs_handle, "timeout_ms", config->timeout_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save timeout_ms: %s", esp_err_to_name(err));
    }

    // Save max_attempts
    err = nvs_set_u8(nvs_handle, "max_attempts", config->max_attempts);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save max_attempts: %s", esp_err_to_name(err));
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Security config saved successfully");
        
        // Create log entry for configuration change
        char config_details[64];
        snprintf(config_details, sizeof(config_details), 
                 "PIN:%s timeout:%lums attempts:%d",
                 config->pin_enabled ? "enabled" : "disabled",
                 config->timeout_ms,
                 config->max_attempts);
        
        dtmf_add_security_log(CMD_CONFIG_CHANGE, true, "config_update", "web_interface", config_details);
        
        // Update runtime configuration
        memcpy(&security_config, config, sizeof(dtmf_security_config_t));
    }

    nvs_close(nvs_handle);
}

// Get current security configuration
void dtmf_get_security_config(dtmf_security_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return;
    }
    
    memcpy(config, &security_config, sizeof(dtmf_security_config_t));
}

// Get current time in milliseconds
static uint32_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

// Map RFC 4733 event code to DTMF character
static char event_to_char(uint8_t event)
{
    switch (event) {
        case 0: return '0';
        case 1: return '1';
        case 2: return '2';
        case 3: return '3';
        case 4: return '4';
        case 5: return '5';
        case 6: return '6';
        case 7: return '7';
        case 8: return '8';
        case 9: return '9';
        case 10: return '*';
        case 11: return '#';
        case 12: return 'A';
        case 13: return 'B';
        case 14: return 'C';
        case 15: return 'D';
        default: return '\0';
    }
}

// Add entry to security log (thread-safe)
static void dtmf_add_security_log(dtmf_command_type_t type, bool success, 
                                   const char* command, const char* caller_id, 
                                   const char* reason)
{
    if (security_log_mutex == NULL) {
        ESP_LOGE(TAG, "Security log mutex not initialized");
        return;
    }

    if (xSemaphoreTake(security_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Get timestamp - use NTP if available, otherwise use system time
        uint64_t timestamp;
        if (ntp_is_synced()) {
            timestamp = ntp_get_timestamp_ms();
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            timestamp = tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
        }

        // Add entry to circular buffer
        dtmf_security_log_t* entry = &security_log_buffer[security_log_head];
        entry->timestamp = timestamp;
        entry->type = type;
        entry->success = success;
        
        // Copy command string
        if (command) {
            snprintf(entry->command, sizeof(entry->command), "%s", command);
        } else {
            entry->command[0] = '\0';
        }
        
        // Copy caller ID
        if (caller_id) {
            snprintf(entry->caller_id, sizeof(entry->caller_id), "%s", caller_id);
        } else {
            snprintf(entry->caller_id, sizeof(entry->caller_id), "unknown");
        }
        
        // Copy reason (for failures)
        if (reason) {
            snprintf(entry->reason, sizeof(entry->reason), "%s", reason);
        } else {
            entry->reason[0] = '\0';
        }

        // Update circular buffer pointers
        security_log_head = (security_log_head + 1) % SECURITY_LOG_SIZE;
        if (security_log_count < SECURITY_LOG_SIZE) {
            security_log_count++;
        }

        xSemaphoreGive(security_log_mutex);
        
        ESP_LOGI(TAG, "Security log entry added: type=%d, success=%d, command=%s", 
                 type, success, command ? command : "");
    } else {
        ESP_LOGE(TAG, "Failed to acquire security log mutex");
    }
}

// Execute validated command
static void dtmf_execute_command(const char* command)
{
    if (!command) {
        return;
    }

    ESP_LOGI(TAG, "Executing command: %s#", command);

    // Check for light toggle command (*2)
    if (strcmp(command, "*2") == 0) {
        ESP_LOGI(TAG, "Toggling light relay");
        light_relay_toggle();
        
        // Log successful execution
        dtmf_add_security_log(CMD_LIGHT_TOGGLE, true, "*2#", NULL, NULL);
        
        // Clear buffer after successful execution
        memset(command_state.buffer, 0, sizeof(command_state.buffer));
        command_state.buffer_index = 0;
        command_state.start_time_ms = 0;
        return;
    }

    // Door opener command - either legacy "*1" or "*[PIN]"
    if (command[0] == '*') {
        // Legacy mode - accept "*1"
        if (!security_config.pin_enabled && strcmp(command, "*1") == 0) {
            ESP_LOGI(TAG, "Activating door opener (legacy mode)");
            xTaskCreate((TaskFunction_t)door_relay_activate, "door_task", 2048, NULL, 5, NULL);
            
            // Log successful execution
            dtmf_add_security_log(CMD_DOOR_OPEN, true, "*1#", NULL, NULL);
            
            // Clear buffer after successful execution
            memset(command_state.buffer, 0, sizeof(command_state.buffer));
            command_state.buffer_index = 0;
            command_state.start_time_ms = 0;
            return;
        }

        // PIN mode - accept "*[PIN]"
        if (security_config.pin_enabled) {
            ESP_LOGI(TAG, "Activating door opener (PIN authenticated)");
            xTaskCreate((TaskFunction_t)door_relay_activate, "door_task", 2048, NULL, 5, NULL);
            
            // Log successful execution (don't log actual PIN)
            char log_command[16];
            snprintf(log_command, sizeof(log_command), "*[PIN]#");
            dtmf_add_security_log(CMD_DOOR_OPEN, true, log_command, NULL, NULL);
            
            // Clear buffer after successful execution
            memset(command_state.buffer, 0, sizeof(command_state.buffer));
            command_state.buffer_index = 0;
            command_state.start_time_ms = 0;
            return;
        }
    }

    ESP_LOGW(TAG, "Command execution failed - unknown command: %s", command);
}

// Constant-time string comparison for PIN validation
static bool constant_time_compare(const char* a, const char* b, size_t len)
{
    uint8_t result = 0;
    for (size_t i = 0; i < len; i++) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

// Validate command format and PIN
static bool dtmf_validate_command(const char* command)
{
    if (!command || command[0] == '\0') {
        ESP_LOGW(TAG, "Empty command");
        command_state.failed_attempts++;
        
        // Log failed attempt
        dtmf_add_security_log(CMD_INVALID, false, "", NULL, "empty_command");
        
        // Check rate limiting
        if (command_state.failed_attempts >= security_config.max_attempts) {
            command_state.rate_limited = true;
            ESP_LOGE(TAG, "SECURITY ALERT: Rate limit triggered after %d failed attempts", 
                     command_state.failed_attempts);
            
            // Log rate limiting event
            char reason[32];
            snprintf(reason, sizeof(reason), "rate_limit_%d_attempts", command_state.failed_attempts);
            dtmf_add_security_log(CMD_INVALID, false, "", NULL, reason);
        }
        return false;
    }

    // Check for light toggle command (*2)
    if (strcmp(command, "*2") == 0) {
        ESP_LOGI(TAG, "Valid light toggle command");
        return true;
    }

    // Check for door opener command
    if (command[0] == '*') {
        // If PIN is not enabled, accept legacy "*1" command
        if (!security_config.pin_enabled) {
            if (strcmp(command, "*1") == 0) {
                ESP_LOGI(TAG, "Valid legacy door opener command");
                return true;
            } else {
                ESP_LOGW(TAG, "Invalid legacy command: %s", command);
                command_state.failed_attempts++;
                
                // Log failed attempt
                char log_command[16];
                snprintf(log_command, sizeof(log_command), "%s#", command);
                dtmf_add_security_log(CMD_INVALID, false, log_command, NULL, "invalid_command");
                
                // Check rate limiting
                if (command_state.failed_attempts >= security_config.max_attempts) {
                    command_state.rate_limited = true;
                    ESP_LOGE(TAG, "SECURITY ALERT: Rate limit triggered after %d failed attempts", 
                             command_state.failed_attempts);
                    
                    // Log rate limiting event
                    char reason[32];
                    snprintf(reason, sizeof(reason), "rate_limit_%d_attempts", command_state.failed_attempts);
                    dtmf_add_security_log(CMD_INVALID, false, log_command, NULL, reason);
                }
                return false;
            }
        }

        // PIN is enabled - validate format *[PIN]
        const char* pin_start = command + 1;  // Skip the '*'
        size_t pin_len = strlen(pin_start);
        size_t expected_len = strlen(security_config.pin_code);

        if (pin_len != expected_len) {
            ESP_LOGW(TAG, "Invalid PIN length");
            command_state.failed_attempts++;
            
            // Log failed attempt (don't log actual PIN)
            dtmf_add_security_log(CMD_DOOR_OPEN, false, "*[PIN]#", NULL, "invalid_pin_length");
            
            // Check rate limiting
            if (command_state.failed_attempts >= security_config.max_attempts) {
                command_state.rate_limited = true;
                ESP_LOGE(TAG, "SECURITY ALERT: Rate limit triggered after %d failed attempts", 
                         command_state.failed_attempts);
                
                // Log rate limiting event
                char reason[32];
                snprintf(reason, sizeof(reason), "rate_limit_%d_attempts", command_state.failed_attempts);
                dtmf_add_security_log(CMD_DOOR_OPEN, false, "*[PIN]#", NULL, reason);
            }
            return false;
        }

        // Constant-time comparison to prevent timing attacks
        if (constant_time_compare(pin_start, security_config.pin_code, expected_len)) {
            ESP_LOGI(TAG, "Valid PIN - door opener authorized");
            return true;
        } else {
            ESP_LOGW(TAG, "Invalid PIN");
            command_state.failed_attempts++;
            
            // Log failed attempt (don't log actual PIN)
            dtmf_add_security_log(CMD_DOOR_OPEN, false, "*[PIN]#", NULL, "invalid_pin");
            
            // Check rate limiting
            if (command_state.failed_attempts >= security_config.max_attempts) {
                command_state.rate_limited = true;
                ESP_LOGE(TAG, "SECURITY ALERT: Rate limit triggered after %d failed attempts", 
                         command_state.failed_attempts);
                
                // Log rate limiting event
                char reason[32];
                snprintf(reason, sizeof(reason), "rate_limit_%d_attempts", command_state.failed_attempts);
                dtmf_add_security_log(CMD_DOOR_OPEN, false, "*[PIN]#", NULL, reason);
            }
            return false;
        }
    }

    ESP_LOGW(TAG, "Unknown command format: %s", command);
    command_state.failed_attempts++;
    
    // Log failed attempt
    char log_command[16];
    snprintf(log_command, sizeof(log_command), "%s#", command);
    dtmf_add_security_log(CMD_INVALID, false, log_command, NULL, "unknown_format");
    
    // Check rate limiting
    if (command_state.failed_attempts >= security_config.max_attempts) {
        command_state.rate_limited = true;
        ESP_LOGE(TAG, "SECURITY ALERT: Rate limit triggered after %d failed attempts", 
                 command_state.failed_attempts);
        
        // Log rate limiting event
        char reason[32];
        snprintf(reason, sizeof(reason), "rate_limit_%d_attempts", command_state.failed_attempts);
        dtmf_add_security_log(CMD_INVALID, false, log_command, NULL, reason);
    }
    return false;
}

// Check if command timeout has expired
static bool dtmf_check_timeout(void)
{
    // No timeout if buffer is empty
    if (command_state.buffer_index == 0 || command_state.start_time_ms == 0) {
        return false;
    }

    uint32_t current_time = get_time_ms();
    uint32_t elapsed = current_time - command_state.start_time_ms;

    if (elapsed >= security_config.timeout_ms) {
        ESP_LOGW(TAG, "Command timeout after %lu ms", elapsed);
        
        // Clear buffer and reset to idle state
        memset(command_state.buffer, 0, sizeof(command_state.buffer));
        command_state.buffer_index = 0;
        command_state.start_time_ms = 0;
        
        return true;
    }

    return false;
}

// Process telephone-event from RFC 4733 RTP packets
void dtmf_process_telephone_event(uint8_t event)
{
    // Check if rate limited
    if (command_state.rate_limited) {
        ESP_LOGW(TAG, "Rate limited - ignoring event");
        return;
    }

    // Map event code to character
    char dtmf_char = event_to_char(event);
    if (dtmf_char == '\0') {
        ESP_LOGW(TAG, "Invalid event code: %d", event);
        return;
    }

    ESP_LOGI(TAG, "Telephone-event received: %c (code %d)", dtmf_char, event);

    // Start timeout timer on first digit
    if (command_state.buffer_index == 0) {
        command_state.start_time_ms = get_time_ms();
        ESP_LOGI(TAG, "Command sequence started");
    }

    // Check for timeout before processing
    if (dtmf_check_timeout()) {
        ESP_LOGW(TAG, "Command timeout - buffer cleared");
        return;
    }

    // Handle '#' - end of command
    if (dtmf_char == '#') {
        command_state.buffer[command_state.buffer_index] = '\0';
        ESP_LOGI(TAG, "Command complete: %s#", command_state.buffer);
        
        // Validate and execute command
        if (dtmf_validate_command(command_state.buffer)) {
            dtmf_execute_command(command_state.buffer);
        }
        
        // Clear buffer after processing
        memset(command_state.buffer, 0, sizeof(command_state.buffer));
        command_state.buffer_index = 0;
        command_state.start_time_ms = 0;
    } 
    // Accumulate character in buffer
    else if (command_state.buffer_index < sizeof(command_state.buffer) - 1) {
        command_state.buffer[command_state.buffer_index++] = dtmf_char;
        ESP_LOGD(TAG, "Buffer: %.*s", command_state.buffer_index, command_state.buffer);
    } else {
        ESP_LOGW(TAG, "Command buffer full - clearing");
        memset(command_state.buffer, 0, sizeof(command_state.buffer));
        command_state.buffer_index = 0;
        command_state.start_time_ms = 0;
    }
}

static void dtmf_command_handler(dtmf_tone_t tone)
{
    static char command_buffer[10] = {0};
    static int command_index = 0;
    
    ESP_LOGI(TAG, "DTMF tone received: %c", tone);
    
    if (tone == DTMF_HASH) {
        // Execute command
        if (strcmp(command_buffer, "*1") == 0) {
            ESP_LOGI(TAG, "Activating door opener");
            xTaskCreate((TaskFunction_t)door_relay_activate, "door_task", 2048, NULL, 5, NULL);
        } else if (strcmp(command_buffer, "*2") == 0) {
            ESP_LOGI(TAG, "Toggling light");
            light_relay_toggle();
        }
        
        // Reset buffer
        memset(command_buffer, 0, sizeof(command_buffer));
        command_index = 0;
    } else if (command_index < sizeof(command_buffer) - 1) {
        command_buffer[command_index++] = (char)tone;
    }
}

void dtmf_decoder_init(void)
{
    ESP_LOGI(TAG, "Initializing DTMF Decoder");
    dtmf_callback = dtmf_command_handler;
    
    // Create mutex for security log
    if (security_log_mutex == NULL) {
        security_log_mutex = xSemaphoreCreateMutex();
        if (security_log_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create security log mutex");
        }
    }
    
    // Load security configuration from NVS
    dtmf_load_security_config();
    
    // Register telephone-event callback with RTP handler
    rtp_set_telephone_event_callback(dtmf_process_telephone_event);
    ESP_LOGI(TAG, "Telephone-event callback registered");
    
    ESP_LOGI(TAG, "DTMF Decoder initialized");
}

void dtmf_set_callback(dtmf_callback_t callback)
{
    dtmf_callback = callback;
}

// Reset call state for new call
void dtmf_reset_call_state(void)
{
    ESP_LOGI(TAG, "Resetting call state");
    
    // Clear command buffer
    memset(command_state.buffer, 0, sizeof(command_state.buffer));
    command_state.buffer_index = 0;
    
    // Reset failed attempts counter
    command_state.failed_attempts = 0;
    
    // Clear rate limit flag
    command_state.rate_limited = false;
    
    // Cancel timeout timer
    command_state.start_time_ms = 0;
    
    // Reset last event timestamp
    command_state.last_event_ts = 0;
    
    ESP_LOGI(TAG, "Call state reset complete");
}

// Get security log entries since timestamp (thread-safe)
int dtmf_get_security_logs(dtmf_security_log_t* entries, int max_entries, uint64_t since_timestamp)
{
    if (!entries || max_entries <= 0) {
        ESP_LOGE(TAG, "Invalid parameters for log retrieval");
        return 0;
    }

    if (security_log_mutex == NULL) {
        ESP_LOGE(TAG, "Security log mutex not initialized");
        return 0;
    }

    int count = 0;

    if (xSemaphoreTake(security_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Calculate the starting index (oldest entry in circular buffer)
        int start_index;
        if (security_log_count < SECURITY_LOG_SIZE) {
            // Buffer not full yet, start from beginning
            start_index = 0;
        } else {
            // Buffer is full, start from head (oldest entry)
            start_index = security_log_head;
        }

        // Iterate through all entries in chronological order
        for (int i = 0; i < security_log_count; i++) {
            if (count >= max_entries) {
                break;
            }
            
            int index = (start_index + i) % SECURITY_LOG_SIZE;
            const dtmf_security_log_t* entry = &security_log_buffer[index];

            // Filter by timestamp
            if (entry->timestamp >= since_timestamp) {
                memcpy(&entries[count], entry, sizeof(dtmf_security_log_t));
                count++;
            }
        }

        xSemaphoreGive(security_log_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire security log mutex for retrieval");
    }

    return count;
}