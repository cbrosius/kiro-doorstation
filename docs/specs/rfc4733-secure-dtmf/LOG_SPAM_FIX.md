# Log Spam Fix - DTMF Security Logs

## Issue Identified

The serial console was being spammed with log messages:
```
I (10656) DTMF: Retrieved 0 security log entries (since timestamp 0)
I (12670) DTMF: Retrieved 0 security log entries (since timestamp 0)
I (14661) DTMF: Retrieved 0 security log entries (since timestamp 0)
...
```

This was happening every 2 seconds, even when there were no log entries to retrieve.

## Root Cause

1. **Web interface polling**: The HTML page was configured to poll `/api/dtmf/logs` every 2 seconds
2. **Verbose logging**: The `dtmf_get_security_logs()` function logged every retrieval, even when returning 0 entries
3. **Unnecessary verbosity**: Configuration changes don't create security log entries (which is correct), but the constant polling created noise

## Changes Made

### 1. Reduced Log Verbosity (`main/dtmf_decoder.c`)

**Before:**
```c
ESP_LOGI(TAG, "Retrieved %d security log entries (since timestamp %llu)", 
         count, since_timestamp);
```

**After:**
```c
// Only log if entries were found (reduce log spam from polling)
if (count > 0) {
    ESP_LOGI(TAG, "Retrieved %d security log entries (since timestamp %llu)", 
             count, since_timestamp);
}
```

**Effect**: Log messages only appear when actual security events are retrieved, not on every empty poll.

### 2. Reduced Polling Frequency (`main/index.html`)

**Before:**
```javascript
// Refresh DTMF logs every 2 seconds
setInterval(refreshDtmfLogs, 2000);
```

**After:**
```javascript
// Refresh DTMF logs every 5 seconds (reduced from 2 to minimize log spam)
setInterval(refreshDtmfLogs, 5000);
```

**Effect**: Reduces polling frequency by 60%, from 30 requests/minute to 12 requests/minute.

## Expected Behavior After Fix

### Serial Console
- **Before**: Log message every 2 seconds (even with no events)
- **After**: Log messages only when security events occur (commands executed, failed attempts, etc.)

### Web Interface
- **Before**: Polls every 2 seconds
- **After**: Polls every 5 seconds
- **Impact**: Still responsive (5 second delay is acceptable for security logs), but much less network/CPU overhead

## What Still Gets Logged

Security events that **will** create log entries:
- ✅ Successful door opener command (`*[PIN]#` or `*1#`)
- ✅ Successful light toggle command (`*2#`)
- ✅ Failed command attempts (invalid PIN, wrong format)
- ✅ Rate limiting triggers (after max failed attempts)
- ✅ Command timeouts (partial commands that expire)

Configuration changes that **won't** create log entries:
- ❌ Changing PIN code via web interface
- ❌ Enabling/disabling PIN protection
- ❌ Changing timeout or max attempts

This is correct behavior - security logs are for **command execution attempts**, not configuration changes.

## Testing the Fix

### 1. Rebuild and Flash

```bash
idf.py build
idf.py flash
```

### 2. Verify Log Spam is Gone

Open serial console and observe:
- Should see normal startup messages
- Should NOT see constant "Retrieved 0 security log entries" messages
- Should only see DTMF messages when actual events occur

### 3. Test Security Logging Still Works

Make a test call and send DTMF commands:

**Test 1: Valid command**
```
Press: * 1 2 3 4 #
Expected log: "Security log entry added: type=0, success=1, command=*[PIN]#"
```

**Test 2: Invalid command**
```
Press: * 9 9 9 9 #
Expected log: "Security log entry added: type=0, success=0, command=*[PIN]#"
```

**Test 3: Web interface updates**
- Logs should appear in web interface within 5 seconds
- No more constant polling messages in serial console

## Additional Optimization (Optional)

If you want even less polling, you could:

1. **Increase polling interval to 10 seconds**:
   ```javascript
   setInterval(refreshDtmfLogs, 10000);
   ```

2. **Add manual refresh button** (already exists):
   - Users can click "Refresh Logs" for immediate update
   - Auto-refresh can be slower

3. **Implement WebSocket** (future enhancement):
   - Push notifications instead of polling
   - Real-time updates with zero polling overhead
   - More complex to implement

## Files Modified

- `main/dtmf_decoder.c` - Conditional logging in `dtmf_get_security_logs()`
- `main/index.html` - Reduced polling frequency from 2s to 5s

## Summary

The log spam issue is now fixed. The serial console will be much cleaner, showing only meaningful security events. The web interface will still update regularly (every 5 seconds) but with significantly less overhead.
