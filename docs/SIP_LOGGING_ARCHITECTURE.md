# SIP Logging Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         Web Browser                              │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                    index.html                               │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │ │
│  │  │ Connect Btn  │  │Disconnect Btn│  │  SIP Log View   │  │ │
│  │  └──────┬───────┘  └──────┬───────┘  └────────┬────────┘  │ │
│  │         │                  │                    │           │ │
│  │         │ POST             │ POST               │ GET       │ │
│  │         │ /api/sip/connect │ /api/sip/disconnect│ /api/sip/log?since=X │
│  └─────────┼──────────────────┼────────────────────┼───────────┘ │
└────────────┼──────────────────┼────────────────────┼─────────────┘
             │                  │                    │
             │                  │                    │
             ▼                  ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    ESP32 Web Server (Core 0)                     │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                    web_server.c                             │ │
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌──────────┐ │ │
│  │  │post_sip_connect  │  │post_sip_disconnect│  │get_sip_log│ │ │
│  │  │    _handler      │  │    _handler       │  │  _handler │ │ │
│  │  └────────┬─────────┘  └────────┬──────────┘  └─────┬────┘ │ │
│  └───────────┼─────────────────────┼────────────────────┼──────┘ │
└──────────────┼─────────────────────┼────────────────────┼────────┘
               │                     │                    │
               │ sip_connect()       │ sip_disconnect()   │ sip_get_log_entries()
               │                     │                    │
               ▼                     ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    SIP Client (Core 1)                           │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                    sip_client.c                             │ │
│  │                                                              │ │
│  │  ┌──────────────────────────────────────────────────────┐  │ │
│  │  │           Thread-Safe Log Buffer (Mutex)              │  │ │
│  │  │  ┌────────────────────────────────────────────────┐  │  │ │
│  │  │  │  sip_log_buffer[50] (Circular Buffer)          │  │  │ │
│  │  │  │  ┌──────────────────────────────────────────┐  │  │  │ │
│  │  │  │  │ Entry: {timestamp, type, message}        │  │  │  │ │
│  │  │  │  │ Entry: {timestamp, type, message}        │  │  │  │ │
│  │  │  │  │ Entry: {timestamp, type, message}        │  │  │  │ │
│  │  │  │  │ ...                                      │  │  │  │ │
│  │  │  │  └──────────────────────────────────────────┘  │  │  │ │
│  │  │  └────────────────────────────────────────────────┘  │  │ │
│  │  └──────────────────────────────────────────────────────┘  │ │
│  │                                                              │ │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │ │
│  │  │sip_connect() │  │sip_disconnect│  │sip_add_log_entry│  │ │
│  │  │              │  │    ()        │  │      ()         │  │ │
│  │  └──────┬───────┘  └──────┬───────┘  └────────▲────────┘  │ │
│  │         │                  │                    │           │ │
│  │         ▼                  ▼                    │           │ │
│  │  ┌──────────────────────────────────┐          │           │ │
│  │  │      sip_task() - Main Loop      │──────────┘           │ │
│  │  │  - Send/Receive SIP messages     │                      │ │
│  │  │  - Log all events                │                      │ │
│  │  │  - Handle registration           │                      │ │
│  │  └──────────────────────────────────┘                      │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## Data Flow

### 1. Connect to SIP
```
User clicks "Connect to SIP"
    ↓
POST /api/sip/connect
    ↓
post_sip_connect_handler()
    ↓
sip_connect()
    ↓
sip_add_log_entry("info", "SIP connection requested")
    ↓
sip_client_register()
    ↓
Send REGISTER message
    ↓
sip_add_log_entry("sent", "REGISTER sip:...")
```

### 2. Receive SIP Response
```
SIP server sends response
    ↓
sip_task() receives data
    ↓
sip_add_log_entry("received", "SIP/2.0 200 OK...")
    ↓
Process response
    ↓
sip_add_log_entry("info", "Registration successful")
```

### 3. Fetch Logs (Auto-refresh every 500ms)
```
JavaScript timer triggers
    ↓
GET /api/sip/log?since=<last_timestamp>
    ↓
get_sip_log_handler()
    ↓
sip_get_log_entries(entries, 50, since_timestamp)
    ↓
Lock mutex
    ↓
Copy matching entries from circular buffer
    ↓
Unlock mutex
    ↓
Return JSON response
    ↓
JavaScript displays new entries with color coding
    ↓
Auto-scroll to bottom
```

## Thread Safety

### Mutex Protection
```c
// Writing to log (from SIP task on Core 1)
xSemaphoreTake(sip_log_mutex, pdMS_TO_TICKS(100))
    ↓
Write to sip_log_buffer[write_index]
    ↓
Increment write_index (circular)
    ↓
xSemaphoreGive(sip_log_mutex)

// Reading from log (from Web Server on Core 0)
xSemaphoreTake(sip_log_mutex, pdMS_TO_TICKS(100))
    ↓
Copy entries from sip_log_buffer
    ↓
xSemaphoreGive(sip_log_mutex)
```

## Circular Buffer Logic

```
Initial State:
┌───┬───┬───┬───┬───┐
│ 0 │ 1 │ 2 │ 3 │...│ 49 │  (50 entries)
└───┴───┴───┴───┴───┘
  ▲
  write_index = 0
  count = 0

After 3 entries:
┌───┬───┬───┬───┬───┐
│ A │ B │ C │   │...│    │
└───┴───┴───┴───┴───┘
          ▲
          write_index = 3
          count = 3

After 52 entries (buffer full, wrapping):
┌───┬───┬───┬───┬───┐
│ 51│ 52│ 3 │ 4 │...│ 50 │
└───┴───┴───┴───┴───┘
      ▲
      write_index = 2
      count = 50 (max)
      
Oldest entry: index 2 (entry #3)
Newest entry: index 1 (entry #52)
```

## API Endpoints

### POST /api/sip/connect
**Request:**
```http
POST /api/sip/connect HTTP/1.1
Content-Length: 0
```

**Response:**
```json
{
  "status": "success",
  "message": "SIP connection initiated"
}
```

### POST /api/sip/disconnect
**Request:**
```http
POST /api/sip/disconnect HTTP/1.1
Content-Length: 0
```

**Response:**
```json
{
  "status": "success",
  "message": "SIP disconnected"
}
```

### GET /api/sip/log?since=<timestamp>
**Request:**
```http
GET /api/sip/log?since=12345678 HTTP/1.1
```

**Response:**
```json
{
  "entries": [
    {
      "timestamp": 12345679,
      "type": "info",
      "message": "SIP connection requested"
    },
    {
      "timestamp": 12345680,
      "type": "sent",
      "message": "REGISTER sip:server.com SIP/2.0..."
    },
    {
      "timestamp": 12345750,
      "type": "received",
      "message": "SIP/2.0 200 OK..."
    }
  ],
  "count": 3
}
```

## Performance Characteristics

### Memory Usage
- **Log Buffer**: 50 entries × ~280 bytes = ~14 KB
- **Mutex**: ~100 bytes
- **Total**: ~14.1 KB

### CPU Usage
- **Polling**: Every 500ms (minimal impact)
- **Log Writing**: O(1) - direct array access
- **Log Reading**: O(n) where n ≤ 50 (very fast)
- **Mutex Overhead**: Negligible (100ms timeout)

### Network Usage
- **Typical Request**: ~100 bytes
- **Typical Response**: ~500-2000 bytes (depends on new entries)
- **Bandwidth**: ~4-16 KB/s (with 500ms polling)

## Color Coding Reference

| Type     | Color | Hex     | Use Case                    |
|----------|-------|---------|----------------------------|
| error    | Red   | #f48771 | Errors, failures           |
| info     | Cyan  | #4ec9b0 | Status updates, info       |
| sent     | Yellow| #dcdcaa | Outgoing SIP messages      |
| received | Blue  | #9cdcfe | Incoming SIP messages      |

## Example Log Session

```
[14:23:45] [INFO] SIP task started on Core 1
[14:23:50] [INFO] SIP connection requested
[14:23:50] [SENT] REGISTER sip:server.com SIP/2.0
Via: SIP/2.0/UDP 192.168.1.100:5060;branch=z9hG4bK12345
From: <sip:user@server.com>;tag=67890
...
[14:23:51] [RECEIVED] SIP/2.0 401 Unauthorized
WWW-Authenticate: Digest realm="server.com"
...
[14:23:51] [INFO] Authentication required
[14:23:51] [SENT] REGISTER sip:server.com SIP/2.0
Authorization: Digest username="user"...
...
[14:23:52] [RECEIVED] SIP/2.0 200 OK
Contact: <sip:user@192.168.1.100:5060>
Expires: 3600
...
[14:23:52] [INFO] SIP registration successful
```

## Dual-Core Architecture

```
┌─────────────────────────────────────────┐
│           ESP32 Dual Core                │
│                                          │
│  ┌────────────────┐  ┌────────────────┐ │
│  │   Core 0       │  │   Core 1       │ │
│  │  (PRO CPU)     │  │  (APP CPU)     │ │
│  │                │  │                │ │
│  │  - WiFi        │  │  - SIP Task    │ │
│  │  - Web Server  │  │  - Logging     │ │
│  │  - HTTP        │  │  - Audio       │ │
│  │                │  │                │ │
│  └────────┬───────┘  └───────▲────────┘ │
│           │                   │          │
│           │    Mutex-Protected│          │
│           │    Shared Memory  │          │
│           └───────────────────┘          │
│                                          │
└─────────────────────────────────────────┘
```

**Benefits:**
- WiFi stability (Core 0 dedicated)
- No beacon timeouts
- Thread-safe log access
- Efficient resource utilization
