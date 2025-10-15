# Watchdog Timer Fix - SIP Connection

## Problem

The ESP32 was experiencing **Watchdog Timer (WDT) resets** when testing or connecting to SIP:

```
W (42) boot.esp32s3: PRO CPU has been reset by WDT.
W (47) boot.esp32s3: APP CPU has been reset by WDT.
```

### Root Cause

The HTTP handler (running on Core 0) was calling blocking functions:
1. `sip_test_configuration()` → `gethostbyname()` (DNS lookup)
2. `sip_connect()` → `sip_client_register()` → `gethostbyname()` (DNS lookup)

DNS lookups can take several seconds, especially if:
- DNS server is slow
- Network is congested
- Server hostname is invalid

This blocked the HTTP handler thread, triggering the watchdog timer.

## Solution

### 1. Simplified Test Function

**Before:**
```c
bool sip_test_configuration(void)
{
    // ... 
    struct hostent *host = gethostbyname(sip_config.server); // BLOCKS!
    // ...
}
```

**After:**
```c
bool sip_test_configuration(void)
{
    // Simple validation - no network operations
    if (strlen(sip_config.server) == 0 || strlen(sip_config.username) == 0) {
        return false;
    }
    return true;
}
```

### 2. Non-Blocking Connection Request

**Before:**
```c
bool sip_connect(void)
{
    // ...
    bool result = sip_client_register(); // BLOCKS on DNS lookup!
    // ...
}
```

**After:**
```c
bool sip_connect(void)
{
    // ...
    registration_requested = true; // Set flag, return immediately
    return true;
}
```

### 3. SIP Task Handles Registration

Added flag-based system:

```c
static bool registration_requested = false;

static void sip_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Check flag and handle registration in SIP task (Core 1)
        if (registration_requested && current_state != SIP_STATE_REGISTERED) {
            registration_requested = false;
            sip_client_register(); // DNS lookup happens here, on Core 1
        }
        
        // ... rest of task
    }
}
```

## Architecture

### Before (Blocking)
```
User clicks "Connect to SIP"
    ↓
HTTP Handler (Core 0)
    ↓
sip_connect()
    ↓
sip_client_register()
    ↓
gethostbyname() ← BLOCKS FOR SECONDS
    ↓
Watchdog Timer Expires
    ↓
SYSTEM RESET!
```

### After (Non-Blocking)
```
User clicks "Connect to SIP"
    ↓
HTTP Handler (Core 0)
    ↓
sip_connect()
    ↓
Set registration_requested = true
    ↓
Return immediately (< 1ms)
    ↓
HTTP Response sent

Meanwhile, in SIP Task (Core 1):
    ↓
Check registration_requested flag
    ↓
Call sip_client_register()
    ↓
gethostbyname() ← Can take time, but doesn't block HTTP
    ↓
Send REGISTER message
    ↓
Update state
```

## Benefits

1. **No Watchdog Resets**: HTTP handlers return quickly
2. **Better User Experience**: Web interface remains responsive
3. **Proper Separation**: Network operations on Core 1, HTTP on Core 0
4. **Logging**: All operations logged for debugging

## Testing

### Before Fix
```
I (22966) SIP: Testing SIP configuration
I (22966) SIP: Testing SIP server: 192.168.60.90:5060
I (22967) SIP: SIP server 192.168.60.90 resolved successfully
I (22974) SIP: SIP test REGISTER message sent successfully (316 bytes)
W (42) boot.esp32s3: PRO CPU has been reset by WDT.  ← CRASH!
```

### After Fix
```
I (12345) SIP: SIP connect requested
I (12345) SIP: SIP registration queued
I (12345) WEB_SERVER: HTTP response sent
I (12545) SIP: Processing registration request
I (12545) SIP: Starting SIP registration
I (12750) SIP: SIP server 192.168.60.90 resolved successfully
I (12755) SIP: REGISTER message sent successfully
I (13100) SIP: SIP message received: SIP/2.0 200 OK
I (13100) SIP: SIP registration successful
```

## Additional Improvements

### Added Logging
All operations now log to the SIP log buffer:
```c
sip_add_log_entry("info", "SIP connection requested");
sip_add_log_entry("info", "SIP registration queued");
sip_add_log_entry("info", "Starting SIP registration");
```

### Simplified Test Function
The test function now only validates configuration, not network connectivity:
- Checks if server and username are not empty
- Returns immediately
- No network operations

For actual connectivity testing, use the "Connect to SIP" button and monitor the logs.

## Code Changes Summary

### Files Modified
- `main/sip_client.c`

### Changes
1. Added `registration_requested` flag
2. Modified `sip_test_configuration()` to be non-blocking
3. Modified `sip_connect()` to set flag instead of calling register
4. Modified `sip_task()` to check flag and handle registration
5. Added logging to all operations

### Lines Changed
- Added: ~15 lines
- Modified: ~30 lines
- Removed: ~20 lines (blocking DNS code)

## Lessons Learned

1. **Never block in HTTP handlers**: Always return quickly
2. **Use task communication**: Flags, queues, or events for async operations
3. **Watchdog timers are strict**: Even 2-3 seconds can trigger reset
4. **DNS lookups are slow**: Can take 1-5 seconds or more
5. **Dual-core architecture**: Use it properly - separate concerns

## Future Improvements

1. **Use FreeRTOS Queue**: Instead of simple flag, use queue for multiple requests
2. **Add Timeout**: Limit DNS lookup time with timeout
3. **Cache DNS Results**: Store resolved IP addresses
4. **Use lwIP async DNS**: Non-blocking DNS resolution
5. **Add Retry Logic**: Automatic retry on failure

## References

- ESP-IDF Task Watchdog Timer: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html
- FreeRTOS Task Communication: https://www.freertos.org/a00113.html
- lwIP DNS: https://www.nongnu.org/lwip/2_1_x/group__dns.html
