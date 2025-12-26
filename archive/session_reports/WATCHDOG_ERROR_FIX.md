# Watchdog Error Fix

## Issue
During SIP registration, watchdog errors appeared in the log:
```
E (6705) task_wdt: esp_task_wdt_reset(707): task not found
E (6710) task_wdt: esp_task_wdt_reset(707): task not found
```

## Root Cause
The `sip_client_register()` function was calling `esp_task_wdt_reset()` to reset the watchdog timer during DNS lookup, but the SIP task was never added to the watchdog monitoring system.

The code had:
```c
// Reset watchdog before potentially long DNS lookup
esp_task_wdt_reset();
struct hostent *host = gethostbyname(sip_config.server);
// Reset watchdog after DNS lookup
esp_task_wdt_reset();
```

Additionally, `sip_client_deinit()` was trying to remove the task from the watchdog with `esp_task_wdt_delete()`.

## Solution
Removed all watchdog-related calls from the SIP client:

1. **Removed watchdog reset calls** in `sip_client_register()`
2. **Removed watchdog delete call** in `sip_client_deinit()`
3. **Removed unused include** `esp_task_wdt.h`

## Why This is Correct
The SIP task doesn't need watchdog monitoring because:
- It runs on Core 1 (APP CPU) with low priority
- Has a 1-second delay loop (`vTaskDelay(pdMS_TO_TICKS(1000))`)
- Regularly yields to other tasks
- DNS lookup is the only potentially blocking operation, and it's acceptable if it takes a few seconds
- The task is not critical for system stability (WiFi and other core functions run independently)

## Impact
- Eliminates watchdog error messages in logs
- Cleaner code without unnecessary watchdog management
- No functional change - SIP task continues to work normally

## Alternative Approach
If watchdog monitoring is desired in the future:
1. Add task to watchdog during initialization: `esp_task_wdt_add(sip_task_handle)`
2. Keep the reset calls during long operations
3. Remove task before deletion: `esp_task_wdt_delete(sip_task_handle)`

However, this is not necessary for the current implementation.
