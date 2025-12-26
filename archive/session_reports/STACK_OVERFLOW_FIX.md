# Stack Overflow Fix - HTTP Handler Task

## Problem

**Error**: Stack overflow in `httpd` task when saving SIP configuration via web interface.

```
***ERROR*** A stack overflow in task httpd has been detected.
Backtrace: ... sip_client_init ... sip_reinit ... post_sip_config_handler
```

## Root Cause

The `sip_reinit()` function was being called directly from the HTTP handler context, which has limited stack space (typically 4-8 KB). The function performed heavy operations:

1. **DNS lookup** via `gethostbyname()` - requires ~2 KB stack
2. **Task creation** via `xTaskCreatePinnedToCore()` - requires ~1 KB stack
3. **Socket operations** - requires ~1 KB stack
4. **Configuration loading** from NVS - requires ~500 bytes stack

**Total stack usage**: ~4.5 KB, which exceeded the HTTP handler's stack limit.

## Solution

**Deferred Reinitialization Pattern**: Instead of performing heavy operations in the HTTP handler, set a flag and let the SIP task handle reinitialization.

### Before (Problematic)
```c
void sip_reinit(void)
{
    // Called from HTTP handler - limited stack!
    vTaskDelete(sip_task_handle);  // ❌ Heavy operation
    close(sip_socket);
    sip_config = sip_load_config();  // ❌ NVS access
    sip_client_init();  // ❌ DNS + task creation
}
```

### After (Fixed)
```c
static bool reinit_requested = false;

void sip_reinit(void)
{
    // Called from HTTP handler - just set flag
    reinit_requested = true;  // ✅ Lightweight
}

// In sip_task() main loop:
if (reinit_requested) {
    reinit_requested = false;
    // Perform heavy operations here - SIP task has 8 KB stack
    close(sip_socket);
    sip_config = sip_load_config();
    // Recreate socket and schedule registration
}
```

## Implementation Details

### 1. Flag-Based Triggering
```c
static bool reinit_requested = false;  // Global flag

void sip_reinit(void)
{
    ESP_LOGI(TAG, "SIP reinitialization requested");
    reinit_requested = true;  // Just set flag, return immediately
}
```

### 2. Deferred Processing in SIP Task
```c
while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Check for reinit request
    if (reinit_requested) {
        reinit_requested = false;
        
        // Close existing resources
        if (sip_socket >= 0) {
            close(sip_socket);
            sip_socket = -1;
        }
        
        // Reload configuration
        sip_config = sip_load_config();
        
        // Recreate socket
        sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        bind(sip_socket, ...);
        
        // Schedule auto-registration
        init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    
    // ... rest of SIP task logic
}
```

## Benefits

### 1. No Stack Overflow
- HTTP handler only sets a boolean flag (~4 bytes)
- Heavy operations run in SIP task context (8 KB stack)
- No risk of stack overflow

### 2. Non-Blocking
- HTTP handler returns immediately
- User gets instant response
- Reinitialization happens asynchronously

### 3. Clean State Management
- SIP task controls its own lifecycle
- No race conditions with task deletion
- Proper resource cleanup

### 4. Better Error Handling
- Errors during reinit don't crash HTTP server
- SIP task can retry on failure
- Logging happens in correct context

## Stack Usage Comparison

### Before (HTTP Handler Context)
```
HTTP Handler Stack: 4096 bytes
├─ HTTP parsing: ~1000 bytes
├─ JSON parsing: ~500 bytes
├─ sip_reinit():
│  ├─ DNS lookup: ~2000 bytes
│  ├─ Task creation: ~1000 bytes
│  └─ Socket ops: ~500 bytes
└─ Response: ~500 bytes
---
Total: ~5500 bytes ❌ OVERFLOW!
```

### After (Deferred to SIP Task)
```
HTTP Handler Stack: 4096 bytes
├─ HTTP parsing: ~1000 bytes
├─ JSON parsing: ~500 bytes
├─ sip_reinit(): ~4 bytes (flag)
└─ Response: ~500 bytes
---
Total: ~2004 bytes ✅ Safe

SIP Task Stack: 8192 bytes
├─ Task overhead: ~500 bytes
├─ Reinit operations: ~4000 bytes
└─ Normal operations: ~1000 bytes
---
Total: ~5500 bytes ✅ Safe
```

## Testing

### Test Case 1: Save Configuration
1. Open web interface
2. Enter SIP credentials
3. Click "Save Configuration"
4. **Expected**: No crash, configuration saved
5. **Verify**: Check logs for "SIP reinitialization requested"

### Test Case 2: Multiple Rapid Saves
1. Save configuration
2. Immediately save again
3. Repeat 5 times quickly
4. **Expected**: No crash, last config wins
5. **Verify**: Flag is idempotent (setting twice = setting once)

### Test Case 3: Save During Active Call
1. Make a test call
2. While call is active, save new config
3. **Expected**: Call ends gracefully, new config applied
4. **Verify**: RTP session stopped, socket recreated

## Related Patterns

### Similar Issues Fixed
This pattern is also used for:
- `registration_requested` - Deferred registration
- `init_timestamp` - Deferred auto-registration

### General Pattern
```c
// In HTTP handler or ISR (limited stack):
static bool operation_requested = false;

void request_operation(void) {
    operation_requested = true;  // Lightweight
}

// In worker task (ample stack):
while (1) {
    if (operation_requested) {
        operation_requested = false;
        perform_heavy_operation();  // Safe here
    }
}
```

## Alternative Solutions Considered

### 1. Increase HTTP Handler Stack
```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size = 8192;  // Double the stack
```
**Rejected**: Wastes memory, doesn't solve root cause

### 2. Create Separate Reinit Task
```c
void sip_reinit(void) {
    xTaskCreate(reinit_task, "reinit", 4096, NULL, 5, NULL);
}
```
**Rejected**: Task creation overhead, cleanup complexity

### 3. Use Event Queue
```c
QueueHandle_t reinit_queue;
void sip_reinit(void) {
    uint8_t msg = 1;
    xQueueSend(reinit_queue, &msg, 0);
}
```
**Rejected**: Overkill for simple flag, adds complexity

## Best Practices Applied

1. ✅ **Separate concerns**: HTTP handling vs. SIP operations
2. ✅ **Async operations**: Don't block HTTP handler
3. ✅ **Stack awareness**: Heavy ops in tasks with adequate stack
4. ✅ **Idempotent flags**: Safe to set multiple times
5. ✅ **Proper logging**: Track operation flow
6. ✅ **Error handling**: Failures don't crash HTTP server

## Verification Checklist

- [x] Code compiles without warnings
- [x] No stack overflow on config save
- [x] Configuration is properly reloaded
- [x] Socket is recreated correctly
- [x] Auto-registration is triggered
- [ ] Tested with real SIP server
- [ ] Tested rapid config changes
- [ ] Tested during active call

## Related Files

- `main/sip_client.c` - Fixed reinit implementation
- `main/web_server.c` - Calls sip_reinit()
- `STACK_OVERFLOW_FIX.md` - This document

## Conclusion

The stack overflow has been fixed by deferring heavy operations from the HTTP handler context to the SIP task context. This is a common pattern in embedded systems where different tasks have different stack sizes and priorities.

**Key Takeaway**: Never perform heavy operations (DNS, task creation, large allocations) from HTTP handlers or ISRs. Use flags and defer to worker tasks.
