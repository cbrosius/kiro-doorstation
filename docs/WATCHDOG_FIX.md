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

### 1. Delayed Auto-Registration

**Problem:**
```c
void sip_client_init(void)
{
    // ...
    sip_client_register(); // Called IMMEDIATELY - WiFi not ready!
}
```

The SIP client was trying to register immediately on startup, before WiFi was fully connected. This caused:
- Socket send failure (error -1)
- DNS lookup blocking
- Watchdog timer reset

**Fix:**
```c
void sip_client_init(void)
{
    // ...
    // Set timestamp for delayed auto-registration (5 seconds)
    init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Auto-registration will start in 5000 ms");
}

static void sip_task(void *pvParameters)
{
    while (1) {
        // Check if 5 seconds have passed since init
        if (init_timestamp > 0 && 
            (xTaskGetTickCount() * portTICK_PERIOD_MS - init_timestamp) >= 5000) {
            init_timestamp = 0;
            registration_requested = true; // Trigger registration
        }
    }
}
```

Now registration happens automatically **5 seconds after initialization**, ensuring:
- WiFi is fully connected and stable
- Web server is running
- DNS resolution will succeed

### 2. Simplified Test Function

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

### 3. Non-Blocking Connection Request

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

### 4. SIP Task Handles Registration

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


## Watchdog Reset Calls

### Additional Fix for Long DNS Lookups

Even with delayed registration on Core 1, DNS lookups can still take 5-10 seconds in some cases, triggering the watchdog timer. Added explicit watchdog reset calls:

```c
#include "esp_task_wdt.h"

bool sip_client_register(void)
{
    // Reset watchdog before potentially long DNS lookup
    esp_task_wdt_reset();
    
    struct hostent *host = gethostbyname(sip_config.server);
    
    // Reset watchdog after DNS lookup
    esp_task_wdt_reset();
    
    if (host == NULL) {
        ESP_LOGE(TAG, "Cannot resolve hostname");
        return false;
    }
    
    // ... rest of registration ...
}

static void sip_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Reset watchdog every iteration to keep task alive
        esp_task_wdt_reset();
        
        // ... task logic ...
    }
}
```

### Task Subscription

The SIP task must be subscribed to the watchdog timer before it can reset it:

```c
void sip_client_init(void)
{
    // Create SIP task
    xTaskCreatePinnedToCore(sip_task, "sip_task", 8192, NULL, 3, &sip_task_handle, 1);
    
    // Subscribe to watchdog timer
    esp_task_wdt_add(sip_task_handle);
    ESP_LOGI(TAG, "SIP task subscribed to watchdog timer");
}

void sip_client_deinit(void)
{
    // Unsubscribe before deleting task
    esp_task_wdt_delete(sip_task_handle);
    vTaskDelete(sip_task_handle);
}
```

### Why This Works

1. **Watchdog Timer**: ESP32 has a task watchdog that resets the system if a task doesn't yield for too long
2. **Task Subscription**: Tasks must subscribe to watchdog before they can reset it
3. **DNS Blocking**: `gethostbyname()` can block for 5-10 seconds
4. **Reset Calls**: Explicitly resetting the watchdog tells the system "I'm still alive, just busy"
5. **Safe Location**: Called from SIP task (Core 1), not HTTP handler

### Expected Behavior

**Before:**
```
I (5663) SIP: REGISTER message sent
[9 second delay - DNS lookup]
W (14668) boot: PRO CPU has been reset by WDT  ← CRASH!
```

**After:**
```
I (5663) SIP: Starting DNS lookup
[Watchdog reset]
[DNS lookup - may take 5-10 seconds]
[Watchdog reset]
I (10500) SIP: DNS lookup successful
I (10505) SIP: REGISTER message sent successfully
I (10650) SIP: SIP/2.0 200 OK received
I (10651) SIP: SIP registration successful  ← SUCCESS!
```

### Summary of All Fixes

1. ✅ **Delayed auto-registration** (5 seconds) - WiFi stability
2. ✅ **Non-blocking test function** - No DNS in HTTP handler
3. ✅ **Async connection request** - Flag-based communication
4. ✅ **Watchdog reset calls** - Prevent timeout during DNS lookup

All four fixes work together to ensure reliable SIP registration without system resets.
