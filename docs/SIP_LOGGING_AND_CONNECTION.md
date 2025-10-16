# SIP Logging and Connection Management

## Overview
Added real-time SIP logging and manual connection management to the ESP32 SIP Door Station web interface.

## Features Added

### 1. SIP Connection Management
- **Connect to SIP Button**: Manually initiate SIP registration
- **Disconnect Button**: Manually disconnect from SIP server
- API endpoints:
  - `POST /api/sip/connect` - Initiate SIP connection
  - `POST /api/sip/disconnect` - Disconnect from SIP server

### 2. Real-Time SIP Logging
- **Log Display**: Dark-themed console-style log viewer
- **Color-Coded Entries**:
  - 🔴 Red: Error messages
  - 🟢 Cyan: Info messages
  - 🟡 Yellow: Sent messages
  - 🔵 Blue: Received messages
- **Auto-Refresh**: Polls for new log entries every 500ms
- **Manual Controls**:
  - Refresh button for manual updates
  - Clear display button
  - Toggle auto-refresh on/off

### 3. Backend Implementation

#### sip_client.c
- `sip_get_log_entries()`: Retrieves log entries with timestamp filtering
- `sip_connect()`: Initiates SIP registration
- `sip_disconnect()`: Closes SIP connection
- Thread-safe log buffer (50 entries, circular buffer)

#### web_server.c
- `GET /api/sip/log?since=<timestamp>`: Returns log entries since timestamp
- `POST /api/sip/connect`: Triggers SIP connection
- `POST /api/sip/disconnect`: Triggers SIP disconnection

#### index.html
- Real-time log viewer with auto-scroll
- Connection management buttons
- Auto-refresh toggle (500ms polling)

## Usage

### Web Interface
1. **Configure SIP Settings**: Enter server, username, password, URI
2. **Save Configuration**: Click "Save Configuration"
3. **Connect**: Click "Connect to SIP" button
4. **Monitor**: Watch real-time logs in the SIP Connection Log section
5. **Disconnect**: Click "Disconnect" when done

### Log Display
- Automatically scrolls to show latest entries
- Color-coded for easy identification
- Timestamps for each entry
- Can be cleared without losing backend logs

## Technical Details

### Log Buffer
- Maximum 50 entries (circular buffer)
- Thread-safe with mutex protection
- Entries include:
  - Timestamp (milliseconds since boot)
  - Type (info, error, sent, received)
  - Message (up to 256 characters)

### Polling Strategy
- Default: 500ms polling interval
- Only fetches new entries (timestamp filtering)
- Minimal bandwidth usage
- Can be disabled for manual refresh only

## Benefits
1. **Debugging**: Real-time visibility into SIP protocol exchanges
2. **Monitoring**: Track connection status and errors
3. **Control**: Manual connection management for testing
4. **User-Friendly**: Color-coded, auto-scrolling display

## Next Steps
- Add log export functionality
- Implement log filtering by type
- Add search functionality
- Consider WebSocket for push-based updates (more efficient than polling)
