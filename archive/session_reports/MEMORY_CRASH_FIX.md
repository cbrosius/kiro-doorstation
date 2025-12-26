# Memory Crash Fix - cJSON NULL Pointer Dereference

## Issue
The system was crashing with a `LoadProhibited` exception (null pointer dereference) at address `0x00000000` in the `get_sip_log_handler` function at line 439 of `web_server.c`.

## Root Cause
The crash occurred when the system ran out of heap memory. Multiple "Out of memory" errors were logged before the crash:
```
W (121661) httpd_txrx: httpd_resp_send_err: 500 Internal Server Error - Out of memory
W (151681) httpd_txrx: httpd_resp_send_err: 500 Internal Server Error - Out of memory
W (161687) httpd_txrx: httpd_resp_send_err: 500 Internal Server Error - Out of memory
```

When `cJSON_CreateObject()` or `cJSON_CreateArray()` failed due to memory exhaustion, they returned NULL. The code then attempted to use these NULL pointers without checking, causing the crash.

## Backtrace Analysis
```
Backtrace: 0x400556d2:0x3fcc41d0 0x42013683:0x3fcc41e0
--- 0x42013683: get_sip_log_handler at web_server.c:439
```

The crash occurred when trying to add data to a NULL cJSON object.

## Fix Applied
Added comprehensive NULL pointer checks for all cJSON allocation functions in critical handlers:

### 1. `get_sip_log_handler` (SIP logs)
- Check if `cJSON_CreateObject()` returns NULL for root object
- Check if `cJSON_CreateArray()` returns NULL for entries array
- Check if `cJSON_CreateObject()` returns NULL in loop (break if out of memory)
- Check if `cJSON_Print()` returns NULL before sending response

### 2. `get_security_logs_handler` (DTMF security logs)
- Added same NULL checks as above
- Properly clean up allocated memory on failure

### 3. `post_wifi_scan_handler` (WiFi scan results)
- Added NULL checks for root object and networks array
- Break loop if object creation fails in iteration
- Check cJSON_Print() result

### 4. `get_auth_logs_handler` (Audit logs)
- Already had proper NULL checks (no changes needed)

## Error Handling Pattern
All fixed handlers now follow this pattern:

```c
cJSON *root = cJSON_CreateObject();
if (!root) {
    // Clean up any allocated resources
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
}

cJSON *array = cJSON_CreateArray();
if (!array) {
    cJSON_Delete(root);
    // Clean up any allocated resources
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
}

for (int i = 0; i < count; i++) {
    cJSON *entry = cJSON_CreateObject();
    if (!entry) {
        // Out of memory - return what we have so far
        break;
    }
    // ... add data to entry
}

char *json_string = cJSON_Print(root);
if (!json_string) {
    cJSON_Delete(root);
    // Clean up any allocated resources
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
}
```

## Prevention
This fix prevents crashes when the system is under memory pressure by:
1. Gracefully handling memory allocation failures
2. Returning proper HTTP 500 errors instead of crashing
3. Properly cleaning up partially allocated resources
4. Allowing the system to continue operating even when individual requests fail

## Testing Recommendations
1. Test under low memory conditions
2. Monitor heap usage during heavy API usage
3. Test concurrent API requests to multiple endpoints
4. Verify proper error responses when memory is exhausted
