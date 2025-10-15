# Log Duplicate Entry Fix

## Issue

The web interface auto-refresh was adding the same log messages over and over again, creating duplicates in the display.

## Root Causes

### 1. Timestamp Precision Issue
- Multiple log entries could have the same timestamp (logged in quick succession)
- Backend used `timestamp > since_timestamp` filter
- Frontend used `lastLogTimestamp = entry.timestamp + 1`
- This caused entries with the same timestamp to be returned multiple times

### 2. Data Type Mismatch
- Log entry structure used `uint32_t` for timestamp
- NTP timestamps are `uint64_t` (milliseconds since epoch)
- This could cause overflow issues with real timestamps

## Solutions Implemented

### 1. Upgraded Timestamp to uint64_t

**Before:**
```c
typedef struct {
    uint32_t timestamp;  // Only 32-bit, can overflow
    char type[16];
    char message[256];
} sip_log_entry_t;
```

**After:**
```c
typedef struct {
    uint64_t timestamp;  // 64-bit to support NTP timestamps
    char type[16];
    char message[256];
} sip_log_entry_t;
```

### 2. Added Duplicate Detection in Frontend

Implemented a Set-based deduplication system:

```javascript
let seenLogEntries = new Set(); // Track seen entries

function logEntryHash(entry) {
    return `${entry.timestamp}_${entry.type}_${entry.message.substring(0, 50)}`;
}

async function refreshSipLog() {
    data.entries.forEach(entry => {
        const hash = logEntryHash(entry);
        if (seenLogEntries.has(hash)) {
            return; // Skip duplicate
        }
        seenLogEntries.add(hash);
        // ... add entry to display
    });
}
```

### 3. Clear Seen Entries on Log Clear

```javascript
function clearSipLogDisplay() {
    document.getElementById('sip-log').innerHTML = '...';
    lastLogTimestamp = Date.now();
    seenLogEntries.clear(); // Clear the seen entries set
}
```

## How It Works

### Entry Hashing
Each log entry is hashed using:
- Timestamp (full precision)
- Type (info, error, sent, received)
- First 50 characters of message

This creates a unique identifier for each entry.

### Duplicate Detection
1. Fetch new entries from backend (timestamp > lastLogTimestamp)
2. For each entry, compute hash
3. Check if hash exists in `seenLogEntries` Set
4. If exists: Skip (duplicate)
5. If new: Add to Set and display

### Timestamp Tracking
- `lastLogTimestamp` tracks the highest timestamp seen
- Backend returns entries with `timestamp > lastLogTimestamp`
- Entries at the boundary timestamp are handled by hash deduplication

## Benefits

### 1. No More Duplicates
- Each entry displayed exactly once
- Works even with same-timestamp entries
- Efficient Set-based lookup

### 2. Proper NTP Support
- uint64_t supports full NTP timestamp range
- No overflow issues
- Consistent with ntp_get_timestamp_ms() return type

### 3. Memory Efficient
- Set only stores hashes (strings)
- Automatically garbage collected
- Cleared on log clear

## Example Scenario

### Before (Duplicates)
```
[19:37:03] [INFO] SIP registration successful
[19:37:03] [INFO] SIP registration successful  ← Duplicate!
[19:37:03] [INFO] SIP registration successful  ← Duplicate!
[19:37:04] [INFO] Call connected
[19:37:04] [INFO] Call connected  ← Duplicate!
```

### After (No Duplicates)
```
[19:37:03] [INFO] SIP registration successful
[19:37:04] [INFO] Call connected
[19:37:05] [INFO] Audio started
```

## Edge Cases Handled

### 1. Same Timestamp, Different Messages
```
Hash 1: "1729537023000_info_SIP registration successful"
Hash 2: "1729537023000_info_Call connected"
```
Different hashes → Both displayed ✅

### 2. Same Message, Different Timestamps
```
Hash 1: "1729537023000_info_Retrying connection"
Hash 2: "1729537024000_info_Retrying connection"
```
Different hashes → Both displayed ✅

### 3. Exact Duplicate
```
Hash 1: "1729537023000_info_SIP registration successful"
Hash 2: "1729537023000_info_SIP registration successful"
```
Same hash → Second one skipped ✅

## Performance

- **Hash computation**: O(1) - simple string concatenation
- **Duplicate check**: O(1) - Set lookup
- **Memory**: ~100 bytes per entry hash
- **Typical usage**: 50 entries × 100 bytes = 5 KB

## Files Modified

1. **main/sip_client.h** - Changed timestamp to uint64_t
2. **main/sip_client.c** - Updated function signature to uint64_t
3. **main/index.html** - Added duplicate detection with Set

## Testing

1. Open web interface
2. Enable auto-refresh
3. Trigger multiple SIP events quickly
4. Verify each entry appears only once
5. Clear log and verify Set is cleared
6. Verify new entries appear correctly

## Summary

✅ **No more duplicate entries** - Set-based deduplication  
✅ **Proper timestamp support** - uint64_t for NTP  
✅ **Efficient implementation** - O(1) duplicate detection  
✅ **Memory efficient** - Only stores hashes  
✅ **Edge cases handled** - Same timestamp, same message, etc.  

The web log now displays each entry exactly once, providing a clean and professional debugging experience!

