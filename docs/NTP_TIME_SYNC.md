# NTP Time Synchronization

## Overview

The ESP32 SIP Door Station now includes NTP (Network Time Protocol) time synchronization for accurate timestamps in debugging logs and system operations.

## Features

- ✅ Automatic time synchronization after WiFi connection
- ✅ Configurable NTP server
- ✅ Configurable timezone
- ✅ Persistent configuration (stored in NVS)
- ✅ Web interface for configuration
- ✅ Manual sync trigger
- ✅ Real-time status display
- ✅ Accurate timestamps in SIP logs

## Default Configuration

```c
NTP Server: pool.ntp.org
Timezone: UTC0
```

## Initialization Sequence

```
System Boot
    ↓
WiFi Connection
    ↓
NTP Sync Init
    ├─ Load config from NVS
    ├─ Set timezone
    ├─ Configure SNTP client
    └─ Start time synchronization
    ↓
Time Synced (callback)
    ↓
Accurate timestamps available
```

## Web Interface

### NTP Status Display

Shows:
- **Time Synced**: ✅ Yes / ❌ No
- **Current Time**: YYYY-MM-DD HH:MM:SS (local time)

### NTP Configuration

Fields:
- **NTP Server**: Hostname or IP of NTP server
- **Timezone**: POSIX timezone string

Buttons:
- **Save NTP Configuration**: Save and apply settings
- **Force Sync Now**: Trigger immediate time synchronization

## Timezone Support

The system supports both **friendly timezone names** (like `Europe/Berlin`) and **POSIX timezone strings** (like `CET-1CEST,M3.5.0,M10.5.0/3`).

### Supported Timezone Names

#### Europe
```
Europe/Berlin, Europe/London, Europe/Paris, Europe/Rome
Europe/Madrid, Europe/Amsterdam, Europe/Brussels, Europe/Vienna
Europe/Zurich, Europe/Stockholm, Europe/Oslo, Europe/Copenhagen
Europe/Helsinki, Europe/Athens, Europe/Moscow, Europe/Istanbul
```

#### Americas
```
America/New_York, America/Chicago, America/Denver, America/Los_Angeles
America/Toronto, America/Vancouver, America/Mexico_City
America/Sao_Paulo, America/Buenos_Aires
```

#### Asia
```
Asia/Tokyo, Asia/Shanghai, Asia/Hong_Kong, Asia/Singapore
Asia/Seoul, Asia/Bangkok, Asia/Dubai, Asia/Kolkata
Asia/Karachi, Asia/Tehran
```

#### Australia
```
Australia/Sydney, Australia/Melbourne, Australia/Brisbane
Australia/Perth, Australia/Adelaide
```

#### Pacific
```
Pacific/Auckland, Pacific/Fiji, Pacific/Honolulu
```

#### Africa
```
Africa/Cairo, Africa/Johannesburg, Africa/Lagos, Africa/Nairobi
```

#### UTC
```
UTC, GMT
```

### POSIX Timezone Strings (Alternative)

You can also use POSIX strings directly:

```
UTC:                UTC0
Berlin (CET/CEST):  CET-1CEST,M3.5.0,M10.5.0/3
London (GMT/BST):   GMT0BST,M3.5.0/1,M10.5.0
New York (EST/EDT): EST5EDT,M3.2.0,M11.1.0
Tokyo (JST):        JST-9
```

## API Endpoints

### GET /api/ntp/status

Get current NTP synchronization status.

**Response:**
```json
{
  "synced": true,
  "current_time": "2025-10-15 14:23:45",
  "timestamp_ms": 1729001025000,
  "server": "pool.ntp.org",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"
}
```

### GET /api/ntp/config

Get current NTP configuration.

**Response:**
```json
{
  "server": "pool.ntp.org",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"
}
```

### POST /api/ntp/config

Update NTP configuration.

**Request:**
```json
{
  "server": "time.google.com",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"
}
```

**Response:**
```json
{
  "status": "success",
  "message": "NTP configuration updated"
}
```

### POST /api/ntp/sync

Force immediate time synchronization.

**Response:**
```json
{
  "status": "success",
  "message": "NTP sync initiated"
}
```

## Code Usage

### Check if Time is Synced

```c
#include "ntp_sync.h"

if (ntp_is_synced()) {
    ESP_LOGI(TAG, "Time is synchronized");
}
```

### Get Current Time String

```c
char time_str[64];
ntp_get_time_string(time_str, sizeof(time_str));
ESP_LOGI(TAG, "Current time: %s", time_str);
// Output: Current time: 2025-10-15 14:23:45
```

### Get Timestamp in Milliseconds

```c
uint64_t timestamp_ms = ntp_get_timestamp_ms();
ESP_LOGI(TAG, "Timestamp: %llu ms", timestamp_ms);
```

### Update Configuration

```c
ntp_set_config("time.google.com", "CET-1CEST,M3.5.0,M10.5.0/3");
```

### Force Synchronization

```c
ntp_force_sync();
```

## Logging Integration

### Serial Console Logs (ESP_LOG)

The system includes `NTP_LOG` macros that automatically add real timestamps to serial console output when NTP is synced:

```c
#include "ntp_log.h"

// Use NTP_LOGI instead of ESP_LOGI
NTP_LOGI(TAG, "SIP registration successful");
```

**Output when NTP synced:**
```
I (12345) SIP: [2025-10-15 14:23:45] SIP registration successful
```

**Output when NTP not synced (fallback):**
```
I (12345) SIP: SIP registration successful
```

### Web Interface Logs

The SIP logging system uses NTP timestamps for web display:

```c
// In sip_add_log_entry()
if (ntp_is_synced()) {
    entry->timestamp = ntp_get_timestamp_ms();  // Real time
} else {
    entry->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;  // Fallback
}
```

**Before NTP (tick count):**
```
[00:01:23] [INFO] SIP connection requested
```

**After NTP (real time):**
```
[14:23:45] [INFO] SIP connection requested
```

### Available Logging Macros

```c
NTP_LOGE(TAG, format, ...);  // Error level
NTP_LOGW(TAG, format, ...);  // Warning level
NTP_LOGI(TAG, format, ...);  // Info level
NTP_LOGD(TAG, format, ...);  // Debug level
NTP_LOGV(TAG, format, ...);  // Verbose level
```

All macros automatically fall back to standard ESP_LOG when NTP is not synced.

## Configuration Storage

NTP configuration is stored in NVS (Non-Volatile Storage):

```
Namespace: ntp_config
Keys:
  - server: NTP server hostname/IP
  - timezone: POSIX timezone string
  - configured: Configuration flag
```

## Troubleshooting

### Time Not Syncing

**Check 1: WiFi Connection**
```
I (1234) NTP: Initializing NTP time synchronization
E (1235) NTP: WiFi not connected
```
→ Ensure WiFi is connected before NTP init

**Check 2: NTP Server Reachable**
```
I (5000) NTP: Time synchronized with NTP server  ← Should see this
```
→ If not seen after 10 seconds, check:
- NTP server hostname is correct
- Firewall allows UDP port 123
- DNS resolution working

**Check 3: Timezone Format**
```
I (1234) NTP: Timezone set to: CET-1CEST,M3.5.0,M10.5.0/3
```
→ Verify timezone string is valid POSIX format

### Wrong Time Displayed

**Issue: Time is off by hours**
→ Check timezone configuration
→ Verify timezone string matches your location

**Issue: Time is off by minutes**
→ Wait for NTP sync to complete (can take 5-10 seconds)
→ Try manual sync with "Force Sync Now" button

### Configuration Not Saving

**Check NVS:**
```
I (1234) NTP: Saving NTP configuration
I (1235) NTP: NTP configuration saved: server=pool.ntp.org, timezone=UTC0
```
→ If error, check NVS partition is initialized

## Popular NTP Servers

### Public NTP Pools

```
pool.ntp.org          - Global pool
time.google.com       - Google NTP
time.cloudflare.com   - Cloudflare NTP
time.windows.com      - Microsoft NTP
time.apple.com        - Apple NTP
```

### Regional Pools

```
europe.pool.ntp.org
north-america.pool.ntp.org
asia.pool.ntp.org
oceania.pool.ntp.org
```

### Country-Specific

```
de.pool.ntp.org       - Germany
us.pool.ntp.org       - United States
uk.pool.ntp.org       - United Kingdom
jp.pool.ntp.org       - Japan
```

## Performance Impact

- **Memory**: ~200 bytes for configuration
- **CPU**: Minimal (SNTP runs in background)
- **Network**: ~100 bytes every sync (default: every hour)
- **Boot Time**: +0-5 seconds for initial sync

## Benefits

### 1. Accurate Debugging

**Before:**
```
[00:01:23] [ERROR] SIP registration failed
[00:01:45] [INFO] Retrying connection
```
Hard to correlate with external logs.

**After:**
```
[14:23:45] [ERROR] SIP registration failed
[14:24:07] [INFO] Retrying connection
```
Easy to correlate with SIP server logs, network captures, etc.

### 2. Log Correlation

Match ESP32 logs with:
- SIP server logs
- Wireshark captures
- System logs
- Other devices

### 3. Timezone Support

Display times in local timezone:
- User-friendly
- Matches local time
- Daylight saving time handled automatically

### 4. Persistent Configuration

- Survives reboots
- No need to reconfigure
- Stored in NVS

## Example Usage Scenario

### Initial Setup

1. Connect ESP32 to WiFi
2. Open web interface
3. Navigate to "NTP Time Synchronization"
4. Enter your NTP server (or use default)
5. Enter your timezone
6. Click "Save NTP Configuration"
7. Wait 5-10 seconds for sync
8. Verify "Time Synced: ✅ Yes"

### Monitoring

- Current time updates every 5 seconds
- SIP logs show accurate timestamps
- Easy to correlate events

### Custom NTP Server

If you have a local NTP server:

1. Enter server IP: `192.168.1.10`
2. Set timezone: `CET-1CEST,M3.5.0,M10.5.0/3`
3. Save configuration
4. Force sync to test

## Summary

NTP time synchronization provides:
- ✅ Accurate timestamps for debugging
- ✅ Easy log correlation
- ✅ Timezone support
- ✅ Configurable via web interface
- ✅ Persistent configuration
- ✅ Automatic synchronization
- ✅ Manual sync option

**Result**: Professional-grade logging with real-world timestamps!

