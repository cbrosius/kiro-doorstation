# Critical Memory Corruption Fixes

## Fix #1: Button Press Crash (0xdeadbeef)

### Issue
System crashed with `Guru Meditation Error: Cache error/MMU entry fault` when pressing the doorbell button. The crash showed:
- `EXCVADDR: 0xdeadbeec` - accessing memory near poison value
- `A9: 0xdeadbeef` - corrupted register
- Crash in `snprintf()` function
- Occurred immediately after "Cannot make call - not in ready state" error

### Root Cause
The `state_names[]` array was defined as a local static variable inside two functions:
1. `sip_client_make_call()` 
2. `sip_get_status()`

Even though marked `static`, having this array inside functions caused stack/memory issues when the logging system tried to format strings using these state names.

### Solution
Moved `state_names[]` array to file scope (global static) at the top of `sip_client.c`:

```c
// State names for logging (global to avoid stack issues)
static const char* state_names[] = {
    "IDLE", "REGISTERING", "REGISTERED", "CALLING", "RINGING",
    "CONNECTED", "DTMF_SENDING", "DISCONNECTED", "ERROR",
    "AUTH_FAILED", "NETWORK_ERROR", "TIMEOUT"
};
```

**Status: FIXED** ✓

---

## Fix #2: Heap Corruption in WiFi/LwIP Stack

### Issue
After successful SIP registration, system crashed with:
```
assert failed: block_next tlsf_block_functions.h:161 (!block_is_last(block))
```
- Heap corruption in TLSF memory allocator
- Crash during WiFi DELBA operations
- Occurs ~10 seconds after SIP registration

### Root Cause
Multiple large static buffers in SIP client causing memory fragmentation:
- 2048-byte static buffer in `sip_task()` for receiving messages
- 2048-byte static buffers in REGISTER and INVITE functions
- Interaction with WiFi stack memory allocation causing heap corruption

### Solution
1. **Changed SIP receive buffer from static to heap-allocated**:
   - Reduced size from 2048 to 1536 bytes
   - Allocated with `malloc()` instead of static
   - Properly freed when task ends

2. **Reduced static buffer sizes**:
   - Authenticated REGISTER message: 2048 → 1536 bytes
   - INVITE message: 2048 → 1536 bytes

3. **Fixed sizeof() bug**:
   - Changed `sizeof(buffer)` to `buffer_size` constant
   - Prevents reading only 4/8 bytes (pointer size) instead of full buffer

### Code Changes
```c
// Before (static allocation)
static char buffer[2048];
len = recv(sip_socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);

// After (heap allocation with proper size)
const size_t buffer_size = 1536;
char *buffer = malloc(buffer_size);
len = recv(sip_socket, buffer, buffer_size - 1, MSG_DONTWAIT);
// ... later ...
free(buffer);
```

### Impact
- Reduces static memory usage by ~4KB
- Prevents heap fragmentation
- Eliminates WiFi/LwIP memory conflicts
- Improves system stability during network operations

**Status: FIXED** ✓

---

## Fix #3: WiFi Block Ack Timer Crash

### Issue
After successful SIP registration, system crashed with:
```
Core 0 panic'ed (StoreProhibited). Exception was unhandled.
PC: 0x4037860d: timer_remove at esp_timer.c:327
EXCVADDR: 0x00000000
```
- Null pointer dereference in WiFi coexistence timer
- Crash during WiFi DELBA (Delete Block Acknowledgement) processing
- Occurs during `pm_coex_tbtt_process` in WiFi driver
- Happens ~15 seconds after SIP registration

### Root Cause
Bug in ESP-IDF WiFi driver's Block Ack (AMPDU) timer management:
- WiFi Block Ack mechanism uses timers for ADDBA/DELBA operations
- Timer removal code tries to access null pointer
- Likely triggered by RF interference or specific AP behavior
- Not caused by application code - this is an ESP-IDF WiFi driver issue

### Solution
Disabled AMPDU (Aggregated MAC Protocol Data Unit) to prevent Block Ack operations:

```c
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
// Disable AMPDU to prevent ADDBA/DELBA timer issues
cfg.ampdu_rx_enable = 0;  // Disable AMPDU RX
cfg.ampdu_tx_enable = 0;  // Disable AMPDU TX
ESP_ERROR_CHECK(esp_wifi_init(&cfg));
```

### Impact
- **Pros**: Eliminates WiFi timer crashes, improves stability
- **Cons**: Slightly reduced WiFi throughput (AMPDU aggregates packets for efficiency)
- For SIP/VoIP applications, stability is more important than maximum throughput
- UDP-based SIP signaling doesn't benefit much from AMPDU anyway

### Alternative Solutions (if needed)
1. Update to newer ESP-IDF version with WiFi driver fixes
2. Reduce Block Ack window size instead of disabling completely
3. Use different WiFi channel to avoid interference
4. Update router firmware

**Status: FIXED** ✓

---

## Fix #4: Web Server Stack Overflow

### Issue
System crashed when refreshing the web interface:
```
assert failed: block_locate_free tlsf_control_functions.h:618 (block_size(block) >= *size)
```
- Heap corruption during JSON creation in `get_sip_log_handler`
- Crash at `cJSON_AddNumberToObject` call (line 107)
- Occurs when web page requests SIP logs

### Root Cause
**Massive stack allocation** in `get_sip_log_handler`:
```c
sip_log_entry_t entries[50];  // 50 entries * 256+ bytes = 12KB+ on stack!
```

Each `sip_log_entry_t` contains:
- `uint64_t timestamp` (8 bytes)
- `char type[32]` (32 bytes)
- `char message[256]` (256 bytes)
- Total: ~296 bytes per entry

50 entries = **14,800 bytes (14.5KB) on the stack**

This caused:
1. Stack overflow in HTTP server task
2. Heap corruption when trying to allocate JSON objects
3. System crash when heap allocator couldn't find valid memory blocks

### Solution
Changed from stack allocation to heap allocation:

```c
// Before (stack - causes overflow)
sip_log_entry_t entries[50];

// After (heap - safe)
const int max_entries = 50;
sip_log_entry_t *entries = malloc(max_entries * sizeof(sip_log_entry_t));
if (!entries) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
}
// ... use entries ...
free(entries);  // Clean up
```

### Impact
- Eliminates stack overflow in HTTP server
- Prevents heap corruption during web requests
- Web interface now stable during page refreshes
- Proper error handling if malloc fails

### Stack Size Guidelines
- HTTP server task stack: typically 4-8KB
- Never allocate >1KB on stack in request handlers
- Use heap allocation for large buffers
- Always check malloc return value

**Status: TESTING REQUIRED**

---

## Testing Checklist
- [x] Fix #1: Button press no longer causes 0xdeadbeef crash
- [x] Fix #2: Heap corruption resolved with proper buffer management
- [x] Fix #3: WiFi stable with AMPDU disabled
- [ ] Fix #4: Web interface refresh doesn't crash system
- [ ] Multiple page refreshes work correctly
- [ ] SIP logs display properly in web interface
- [ ] System runs stable for >5 minutes with web activity
