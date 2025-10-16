# Quick Start: NTP Time Synchronization

## What It Does

Adds accurate real-world timestamps to your ESP32 SIP Door Station for better debugging.

## Before vs After

### Before (Uptime Timestamps)
```
[00:01:23] [INFO] SIP connection requested
[00:01:45] [ERROR] Connection failed
```
Hard to correlate with external logs.

### After (Real Timestamps)
```
[14:23:45] [INFO] SIP connection requested
[14:24:07] [ERROR] Connection failed
```
Easy to match with SIP server logs, Wireshark, etc.

## Quick Setup

### 1. Flash Firmware
```bash
idf.py build flash monitor
```

### 2. Connect to WiFi
- Open web interface
- Connect to your WiFi network

### 3. Configure NTP (Optional)
Default works fine, but you can customize:

**Web Interface → NTP Time Synchronization**
- Server: `pool.ntp.org` (or your own)
- Timezone: `UTC0` (or your timezone)
- Click "Save NTP Configuration"

### 4. Verify
- Check "Time Synced: ✅ Yes"
- Check "Current Time" shows correct time
- Check SIP logs show real timestamps

## Common Timezones

You can use friendly names or POSIX strings:

**Friendly Names (Recommended):**
```
Europe/Berlin
America/New_York
America/Los_Angeles
Europe/London
Asia/Tokyo
Australia/Sydney
UTC
```

**POSIX Strings (Alternative):**
```
CET-1CEST,M3.5.0,M10.5.0/3  (Berlin)
EST5EDT,M3.2.0,M11.1.0       (New York)
PST8PDT,M3.2.0,M11.1.0       (Los Angeles)
GMT0BST,M3.5.0/1,M10.5.0     (London)
JST-9                         (Tokyo)
AEST-10AEDT,M10.1.0,M4.1.0/3 (Sydney)
```

## Troubleshooting

### Time Not Syncing?

**Check WiFi:**
- Ensure WiFi is connected
- Check internet access

**Check NTP Server:**
- Try `time.google.com`
- Or `time.cloudflare.com`

**Force Sync:**
- Click "Force Sync Now" button
- Wait 5-10 seconds

### Wrong Time?

**Check Timezone:**
- Verify timezone string is correct
- Use examples above
- Save and force sync

## API Usage (Optional)

### Check Status
```bash
curl http://192.168.1.100/api/ntp/status
```

### Update Config
```bash
curl -X POST http://192.168.1.100/api/ntp/config \
  -H "Content-Type: application/json" \
  -d '{"server":"time.google.com","timezone":"CET-1CEST,M3.5.0,M10.5.0/3"}'
```

### Force Sync
```bash
curl -X POST http://192.168.1.100/api/ntp/sync
```

## Bonus: Serial Console Timestamps

The system also adds real timestamps to serial console output (ESP_LOG) when using NTP_LOG macros:

**Serial Output:**
```
I (12345) SIP: [2025-10-15 14:23:45] SIP registration successful
I (12456) SIP: [2025-10-15 14:23:56] Call connected
```

This makes debugging even easier by showing real time in both web interface and serial console!

## That's It!

Your ESP32 now has accurate timestamps for professional debugging in both web interface and serial console.

