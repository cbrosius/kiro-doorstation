# Centralized Logging System Design

## Overview

The centralized logging system provides a unified interface for logging security events, operational events, and audit trails across all system components. It replaces the duplicated logging functionality currently spread across auth_manager, cert_manager, and web_server modules.

The system consists of:
- A new `system_log` module with header and implementation files
- In-memory circular buffer for fast log access
- Optional NVS persistence for critical security events
- Thread-safe operations using FreeRTOS mutexes
- Backward-compatible API matching existing auth_log_security_event

## Architecture

### Component Structure

```
┌─────────────────────────────────────────────────────────────┐
│                    System Components                         │
│  (auth_manager, cert_manager, web_server, sip_client, etc.) │
└────────────────────┬────────────────────────────────────────┘
                     │
                     │ log_event()
                     │
                     ▼
┌─────────────────────────────────────────────────────────────┐
│                    system_log.h/c                            │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Public API                                          │   │
│  │  - log_event()                                       │   │
│  │  - log_get_entries()                                 │   │
│  │  - log_init()                                        │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Circular Buffer (In-Memory)                         │   │
│  │  - 100 entries (configurable)                        │   │
│  │  - Thread-safe with mutex                            │   │
│  │  - Newest entries overwrite oldest                   │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  NVS Persistence (Optional)                          │   │
│  │  - Critical events only                              │   │
│  │  - 50 entries max                                    │   │
│  │  - Loaded on init                                    │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

1. Component calls `log_event()` with event details
2. System log validates and sanitizes inputs
3. Mutex acquired for thread safety
4. Entry added to circular buffer
5. If critical event, also persisted to NVS
6. Mutex released
7. Event logged to ESP_LOG for console output

## Components and Interfaces

### system_log.h - Public Interface

```c
#ifndef SYSTEM_LOG_H
#define SYSTEM_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Log entry constants
#define LOG_MAX_ENTRIES 100
#define LOG_EVENT_TYPE_MAX_LEN 32
#define LOG_USERNAME_MAX_LEN 32
#define LOG_IP_ADDRESS_MAX_LEN 16
#define LOG_RESULT_MAX_LEN 64

// Critical event types (persisted to NVS)
#define LOG_CRITICAL_LOGIN_FAILED "login_failed"
#define LOG_CRITICAL_PASSWORD_CHANGE "password_change"
#define LOG_CRITICAL_PASSWORD_RESET "password_reset"
#define LOG_CRITICAL_CERT_UPLOAD "cert_upload"
#define LOG_CRITICAL_CERT_DELETE "cert_delete"
#define LOG_CRITICAL_CONFIG_CHANGE "config_change"

// Log entry structure
typedef struct {
    uint32_t timestamp;
    char event_type[LOG_EVENT_TYPE_MAX_LEN];
    char username[LOG_USERNAME_MAX_LEN];
    char ip_address[LOG_IP_ADDRESS_MAX_LEN];
    char result[LOG_RESULT_MAX_LEN];
    bool success;
} log_entry_t;

/**
 * @brief Initialize logging system
 * 
 * Initializes the circular buffer, mutex, and loads persisted logs from NVS.
 * Must be called before any other logging functions.
 */
void log_init(void);

/**
 * @brief Log an event
 * 
 * @param event_type Type of event (e.g., "login", "cert_upload", "config_change")
 * @param username Username associated with event (can be NULL)
 * @param ip_address IP address associated with event (can be NULL)
 * @param result Result description
 * @param success Whether the event was successful
 */
void log_event(const char* event_type, const char* username, 
               const char* ip_address, const char* result, bool success);

/**
 * @brief Get log entries in reverse chronological order
 * 
 * @param entries Array to store log entries
 * @param max_entries Maximum number of entries to retrieve
 * @return int Actual number of entries retrieved
 */
int log_get_entries(log_entry_t* entries, int max_entries);

/**
 * @brief Get total number of log entries available
 * 
 * @return int Number of log entries in buffer
 */
int log_get_count(void);

/**
 * @brief Clear all log entries (memory and NVS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t log_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_LOG_H
```

### system_log.c - Implementation

Key implementation details:

1. **Circular Buffer Management**
   - Fixed-size array of log_entry_t structures
   - Head pointer tracks next write position
   - Count tracks total entries (up to max)
   - Oldest entries automatically overwritten when full

2. **Thread Safety**
   - FreeRTOS mutex protects all buffer operations
   - Mutex created during log_init()
   - Acquired before writes, released after
   - Timeout on mutex acquisition to prevent deadlocks

3. **NVS Persistence**
   - Separate NVS namespace "system_log"
   - Critical events identified by event_type prefix
   - Stored as blob array with count
   - Loaded during initialization and merged with in-memory buffer

4. **Input Validation**
   - NULL checks for all pointer parameters
   - String truncation for oversized inputs
   - Safe string copying with bounds checking
   - Default values for NULL optional parameters

## Data Models

### log_entry_t Structure

```c
typedef struct {
    uint32_t timestamp;        // Unix timestamp (seconds since epoch)
    char event_type[32];       // Event type identifier
    char username[32];         // Username or "unknown"
    char ip_address[16];       // IP address or "unknown"
    char result[64];           // Human-readable result message
    bool success;              // Success/failure flag
} log_entry_t;
```

Size: 148 bytes per entry
Total memory for 100 entries: ~14.8 KB

### Event Type Categories

1. **Authentication Events**
   - login
   - login_failed
   - logout
   - password_change
   - password_reset
   - session_expired

2. **Certificate Events**
   - cert_upload
   - cert_generate
   - cert_delete
   - cert_validate
   - cert_download

3. **Configuration Events**
   - config_change
   - config_backup
   - config_restore
   - wifi_config
   - sip_config

4. **System Events**
   - system_boot
   - system_reset
   - firmware_update
   - rate_limit

## Error Handling

### Mutex Failures
- Log error to ESP_LOG
- Continue without adding to buffer (graceful degradation)
- Return error code if applicable

### NVS Failures
- Log warning to ESP_LOG
- Continue with in-memory logging only
- Do not block normal operation

### Buffer Full
- Automatically overwrite oldest entry
- No error condition (expected behavior)

### Invalid Parameters
- Use safe defaults ("unknown" for NULL strings)
- Log warning for debugging
- Continue operation

## Testing Strategy

### Unit Testing Approach

1. **Initialization Tests**
   - Verify mutex creation
   - Verify NVS namespace creation
   - Verify buffer initialization

2. **Basic Logging Tests**
   - Single event logging
   - Multiple event logging
   - Buffer wraparound behavior
   - NULL parameter handling

3. **Thread Safety Tests**
   - Concurrent logging from multiple tasks
   - Mutex contention scenarios
   - Deadlock prevention

4. **Persistence Tests**
   - Critical event NVS storage
   - NVS load on initialization
   - NVS error handling

5. **Retrieval Tests**
   - Reverse chronological order
   - Partial retrieval (fewer than max)
   - Empty buffer handling
   - Full buffer retrieval

### Integration Testing

1. **Migration Testing**
   - Verify auth_manager logs correctly
   - Verify cert_manager logs correctly
   - Verify web_server logs correctly
   - Compare output with previous implementation

2. **Performance Testing**
   - Measure logging overhead
   - Verify no blocking on high-frequency events
   - Memory usage validation

3. **Persistence Testing**
   - Reboot with critical events
   - Verify events survive reboot
   - Verify merge with new events

## Migration Strategy

### Phase 1: Create system_log Module
- Implement system_log.h and system_log.c
- Add to CMakeLists.txt
- Initialize in main.c

### Phase 2: Update auth_manager
- Replace internal audit log with system_log calls
- Update auth_log_security_event to call log_event
- Update auth_get_audit_logs to call log_get_entries
- Remove internal audit log buffer and functions

### Phase 3: Update cert_manager
- Replace cert_log_operation with log_event calls
- Remove cert_log_operation function
- Update all certificate operations to log events

### Phase 4: Update web_server
- Add logging for configuration changes
- Add logging for API endpoint access
- Add logging for file uploads/downloads

### Phase 5: Testing and Validation
- Run integration tests
- Verify log output matches expectations
- Performance validation
- Memory usage validation

## Backward Compatibility

### auth_log_security_event Wrapper

To maintain backward compatibility, auth_manager will keep the auth_log_security_event function as a thin wrapper:

```c
void auth_log_security_event(const char* event_type, const char* username, 
                             const char* ip_address, const char* result, bool success) {
    log_event(event_type, username, ip_address, result, success);
}
```

This allows existing code to continue working without changes while new code can call log_event directly.

### audit_log_entry_t Compatibility

The log_entry_t structure matches the existing audit_log_entry_t structure, ensuring binary compatibility for any code that reads log entries.

## Configuration

### Compile-Time Configuration

```c
// In system_log.h
#define LOG_MAX_ENTRIES 100           // Maximum in-memory entries
#define LOG_NVS_MAX_ENTRIES 50        // Maximum NVS-persisted entries
#define LOG_MUTEX_TIMEOUT_MS 1000     // Mutex acquisition timeout
```

### Runtime Configuration

No runtime configuration needed. All settings are compile-time constants for optimal performance and memory usage.

## Performance Considerations

### Memory Usage
- In-memory buffer: ~14.8 KB (100 entries × 148 bytes)
- NVS storage: ~7.4 KB (50 entries × 148 bytes)
- Mutex overhead: ~100 bytes
- Total: ~22.3 KB

### CPU Overhead
- Mutex lock/unlock: ~10-20 microseconds
- String copy operations: ~5-10 microseconds per field
- NVS write (critical events only): ~1-5 milliseconds
- Total per log call: ~50-100 microseconds (non-critical), ~1-5 ms (critical)

### Optimization Strategies
- Use stack-allocated structures where possible
- Minimize string operations
- Batch NVS writes if needed
- Consider async NVS writes for critical events

## Security Considerations

### Sensitive Data
- Passwords never logged
- Session IDs never logged
- Only usernames and IP addresses logged

### Log Tampering
- NVS logs protected by ESP32 flash encryption (if enabled)
- In-memory logs cleared on reboot
- No API to modify existing log entries

### Log Flooding
- Fixed buffer size prevents memory exhaustion
- Oldest entries automatically discarded
- No impact on system stability

## Future Enhancements

1. **Remote Logging**
   - Syslog protocol support
   - MQTT logging integration
   - HTTP POST to remote server

2. **Log Filtering**
   - Filter by event type
   - Filter by username
   - Filter by time range

3. **Log Export**
   - CSV export via web interface
   - JSON export for automation
   - Binary export for analysis tools

4. **Log Rotation**
   - Automatic archival to SD card
   - Compression of old logs
   - Configurable retention policies
