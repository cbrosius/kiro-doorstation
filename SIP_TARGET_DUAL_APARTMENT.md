# SIP Dual Apartment Target Configuration

## Overview
Replaced the single SIP-URI configuration with two separate SIP targets (SIP-Target1 and SIP-Target2) to support addressing two different apartments with the doorbell system.

## Changes Made

### 1. Backend Configuration Structure (`main/sip_client.h`)
- Updated getter functions:
  - Removed: `sip_get_uri()` 
  - Added: `sip_get_target1()` - Returns apartment1_uri
  - Added: `sip_get_target2()` - Returns apartment2_uri

- Updated setter functions:
  - Removed: `sip_set_uri()`
  - Added: `sip_set_target1()` - Sets apartment1_uri
  - Added: `sip_set_target2()` - Sets apartment2_uri

### 2. Backend Implementation (`main/sip_client.c`)
- Implemented new getter/setter functions for both targets
- Configuration structure already supported `apartment1_uri` and `apartment2_uri` fields
- NVS storage already handled both apartment URIs (stored as "apt1" and "apt2")

### 3. Web Server API (`main/web_server.c`)
- **GET /api/sip/config**: Now returns both `target1` and `target2` instead of single `uri`
- **POST /api/sip/config**: Now accepts both `target1` and `target2` fields
- Updated `sip_save_config()` call to save both targets

### 4. Web Interface (`main/index.html`)
- Replaced single "SIP URI" input field with two fields:
  - **SIP-Target1**: For Apartment 1 (e.g., sip:apartment1@domain.com)
  - **SIP-Target2**: For Apartment 2 (e.g., sip:apartment2@domain.com)
- Updated JavaScript to fetch and save both targets
- Updated DTMF information section to clarify:
  - Bell 1 (GPIO 21) calls SIP-Target1
  - Bell 2 (GPIO 22) calls SIP-Target2

### 5. GPIO Handler (`main/gpio_handler.c`)
- Updated doorbell press handlers to use configured targets:
  - Doorbell 1: Calls `sip_get_target1()` dynamically
  - Doorbell 2: Calls `sip_get_target2()` dynamically
- Added validation to check if targets are configured before making calls
- Removed hardcoded "apartment1@example.com" and "apartment2@example.com"

## Configuration Example

### Web Interface
```
SIP-Target1: sip:apartment101@sip.example.com
SIP-Target2: sip:apartment102@sip.example.com
SIP Server: sip.example.com
Username: doorbell
Password: ********
```

### Behavior
- When doorbell button 1 (GPIO 21) or BOOT button (GPIO 0) is pressed → Calls SIP-Target1
- When doorbell button 2 (GPIO 22) is pressed → Calls SIP-Target2
- Both targets can be different apartments, extensions, or phone numbers
- Configuration is stored in NVS and persists across reboots

## Benefits
1. **Flexibility**: Each doorbell button can call a different apartment
2. **Clear Configuration**: Explicit naming makes it obvious which target is which
3. **Backward Compatible**: Existing NVS storage structure already supported this
4. **Dynamic**: Targets can be changed via web interface without recompiling

## Test Call Feature

### Web Interface Test Buttons
Each SIP target now has a dedicated "Test Call" button next to its input field:
- **Target1 Test Call**: Initiates a test call to SIP-Target1
- **Target2 Test Call**: Initiates a test call to SIP-Target2

### API Endpoint
- **POST /api/sip/testcall**: Initiates a test call to a specified target
  - Request body: `{"target": "sip:user@domain.com"}`
  - Response: `{"status": "success/failed", "message": "..."}`
  - Validates that SIP is registered before allowing test calls

### Usage
1. Configure SIP-Target1 and/or SIP-Target2 in the web interface
2. Save the configuration
3. Connect to SIP server (must be registered)
4. Click "Test Call" button next to either target
5. Monitor the SIP log to see the call being initiated
6. The target device should ring

## Testing
1. Configure both SIP-Target1 and SIP-Target2 in the web interface
2. Save configuration and connect to SIP server
3. Use "Test Call" buttons to verify each target works correctly
4. Press doorbell button 1 (or BOOT button) → Should call Target1
5. Press doorbell button 2 → Should call Target2
6. Check SIP logs in web interface to verify correct targets are being called
