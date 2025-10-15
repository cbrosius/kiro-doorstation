# Web Log Timestamp Fix

## Issue

Web interface logs were not displaying real timestamps even when NTP was synced. They showed only the time portion (HH:MM:SS) instead of the full date-time.

## Root Cause

The JavaScript was using `toLocaleTimeString()` which only shows the time component:

```javascript
const time = new Date(entry.timestamp).toLocaleTimeString();
// Output: "19:37:03" (missing date)
```

## Solution

Updated the timestamp formatting logic to:
1. Detect if timestamp is real (NTP synced) or uptime
2. Format accordingly with full date-time for real timestamps

```javascript
// Check if real timestamp (after year 2000)
if (entry.timestamp > 946684800000) {
    // Real timestamp - show full date and time
    const date = new Date(entry.timestamp);
    timeStr = date.toLocaleString('en-GB', { 
        year: 'numeric', 
        month: '2-digit', 
        day: '2-digit',
        hour: '2-digit', 
        minute: '2-digit', 
        second: '2-digit',
        hour12: false 
    });
} else {
    // Uptime - show as HH:MM:SS
    const totalSeconds = Math.floor(entry.timestamp / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    timeStr = `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
}
```

## Result

### Before (NTP Synced)
```
[19:37:03] [INFO] SIP registration successful
```
Missing date, hard to correlate with external logs.

### After (NTP Synced)
```
[15/10/2025, 19:37:03] [INFO] SIP registration successful
```
Full date-time, easy to correlate with serial logs and external systems.

### Before (Not Synced)
```
[5893] [INFO] SIP task started
```
Raw milliseconds, not user-friendly.

### After (Not Synced)
```
[00:01:38] [INFO] SIP task started
```
Formatted as uptime (HH:MM:SS), user-friendly.

## Timestamp Detection

The code uses a threshold of `946684800000` milliseconds (January 1, 2000) to distinguish between:
- **Real timestamps**: > 946684800000 ms (NTP synced)
- **Uptime timestamps**: < 946684800000 ms (boot time)

This works because:
- Real timestamps are in milliseconds since Unix epoch (1970)
- Uptime timestamps are milliseconds since boot (always < 1 day for ESP32)

## Format Details

### Real Timestamp Format
- **Locale**: en-GB (DD/MM/YYYY format)
- **Format**: `15/10/2025, 19:37:03`
- **24-hour**: Yes (hour12: false)
- **Components**: Date + Time

### Uptime Format
- **Format**: `HH:MM:SS`
- **Example**: `00:01:38` (1 minute 38 seconds since boot)
- **Padding**: Zero-padded for consistency

## Consistency

Now both serial console and web interface show consistent timestamps:

**Serial Console:**
```
I (5893) SIP: [2025-10-15 19:37:03] SIP registration successful
```

**Web Interface:**
```
[15/10/2025, 19:37:03] [INFO] SIP registration successful
```

Both show the same real time, making debugging much easier!

## Files Modified

- `main/index.html` - Updated `refreshSipLog()` function

## Testing

1. Open web interface
2. Wait for NTP sync
3. Trigger SIP events
4. Check web log display shows full date-time
5. Compare with serial console timestamps

Expected: Both show same real time in their respective formats.

