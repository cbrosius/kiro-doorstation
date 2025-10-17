# Design Document

## Overview

This design document outlines the implementation of five missing API endpoints for the SIP doorbell web interface. The endpoints follow the existing architectural patterns established in the web_server.c file, using ESP-IDF HTTP server, cJSON for JSON handling, and NVS for persistent storage.

## Architecture

### Component Structure

The implementation follows the existing web server architecture:

```
┌─────────────────────────────────────────────────────┐
│              Web Interface (Browser)                 │
└─────────────────────────────────────────────────────┘
                        │ HTTP/JSON
                        ▼
┌─────────────────────────────────────────────────────┐
│           ESP-IDF HTTP Server (web_server.c)        │
│  ┌──────────────────────────────────────────────┐  │
│  │  New Endpoint Handlers:                      │  │
│  │  - get_wifi_config_handler                   │  │
│  │  - post_wifi_config_handler                  │  │
│  │  - get_network_ip_handler                    │  │
│  │  - get_email_config_handler                  │  │
│  │  - post_email_config_handler                 │  │
│  │  - get_ota_version_handler                   │  │
│  │  - get_system_info_handler                   │  │
│  └──────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ WiFi Manager │ │ ESP-IDF APIs │ │     NVS      │
│   (existing) │ │   (system)   │ │  (storage)   │
└──────────────┘ └──────────────┘ └──────────────┘
```

### Design Principles

1. **Consistency**: Follow existing patterns in web_server.c for handler structure, error handling, and JSON responses
2. **Security**: Never expose sensitive data (passwords) in GET responses
3. **Validation**: Validate all input data before processing or storage
4. **Error Handling**: Return appropriate HTTP status codes and error messages
5. **Minimal Dependencies**: Leverage existing components (wifi_manager, NVS) where possible

## Components and Interfaces

### 1. WiFi Configuration Endpoint

**Endpoint**: `/api/wifi/config`

**GET Handler**: `get_wifi_config_handler`
- Retrieves current WiFi SSID from wifi_manager
- Returns JSON with SSID only (no password)
- Response format:
```json
{
  "ssid": "MyNetwork",
  "configured": true
}
```

**POST Handler**: `post_wifi_config_handler`
- Accepts JSON with ssid and password fields
- Validates JSON structure
- Calls existing `wifi_save_config()` function
- Does not automatically reconnect (user must use existing /api/wifi/connect)
- Response format:
```json
{
  "status": "success",
  "message": "WiFi configuration saved"
}
```

### 2. Network IP Information Endpoint

**Endpoint**: `/api/network/ip`

**GET Handler**: `get_network_ip_handler`
- Uses `esp_netif_get_ip_info()` to retrieve IP configuration
- Uses `esp_netif_get_dns_info()` for DNS information
- Returns comprehensive network information
- Response format:
```json
{
  "ip_address": "192.168.1.100",
  "netmask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns": "8.8.8.8",
  "connected": true
}
```

### 3. Email Configuration Endpoint

**Endpoint**: `/api/email/config`

**Data Structure** (new):
```c
typedef struct {
    char smtp_server[64];
    uint16_t smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char recipient_email[64];
    bool enabled;
    bool configured;
} email_config_t;
```

**GET Handler**: `get_email_config_handler`
- Loads email configuration from NVS
- Returns configuration without password
- Response format:
```json
{
  "smtp_server": "smtp.gmail.com",
  "smtp_port": 587,
  "smtp_username": "doorbell@example.com",
  "recipient_email": "admin@example.com",
  "enabled": true,
  "configured": true
}
```

**POST Handler**: `post_email_config_handler`
- Accepts JSON with email configuration
- Validates SMTP server format (basic validation)
- Validates port range (1-65535)
- Validates email format (contains @)
- Saves to NVS using namespace "email_config"
- Response format:
```json
{
  "status": "success",
  "message": "Email configuration saved"
}
```

**NVS Storage Functions** (new):
- `email_save_config()`: Save email configuration to NVS
- `email_load_config()`: Load email configuration from NVS

### 4. OTA Version Endpoint

**Endpoint**: `/api/ota/version`

**GET Handler**: `get_ota_version_handler`
- Retrieves firmware version from compile-time defines
- Uses `esp_ota_get_app_description()` for app info
- Uses `esp_ota_get_partition_description()` for partition info
- Calculates available OTA space
- Response format:
```json
{
  "version": "v1.0.0",
  "build_date": "Oct 17 2025",
  "build_time": "14:30:00",
  "idf_version": "v5.1.2",
  "project_name": "sip_doorbell",
  "ota_partition_size": 1572864,
  "ota_available_space": 1048576
}
```

### 5. System Information Endpoint

**Endpoint**: `/api/system/info`

**GET Handler**: `get_system_info_handler`
- Uses `esp_chip_info()` for chip details
- Uses `spi_flash_get_chip_size()` for flash size
- Uses `esp_efuse_mac_get_default()` for MAC address
- Combines with existing system status data
- Response format:
```json
{
  "chip_model": "ESP32-S3",
  "chip_revision": 0,
  "cpu_cores": 2,
  "cpu_freq_mhz": 240,
  "flash_size_mb": 8,
  "mac_address": "AA:BB:CC:DD:EE:FF",
  "free_heap_bytes": 245760,
  "uptime_seconds": 3600,
  "firmware_version": "v1.0.0"
}
```

## Data Models

### Email Configuration Structure

```c
typedef struct {
    char smtp_server[64];
    uint16_t smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char recipient_email[64];
    bool enabled;
    bool configured;
} email_config_t;
```

### NVS Keys

Email configuration will use the following NVS namespace and keys:
- Namespace: `"email_config"`
- Keys:
  - `"smtp_server"` (string)
  - `"smtp_port"` (uint16)
  - `"smtp_user"` (string)
  - `"smtp_pass"` (string)
  - `"recipient"` (string)
  - `"enabled"` (uint8)

## Error Handling

### HTTP Status Codes

- `200 OK`: Successful GET request
- `400 Bad Request`: Invalid JSON, missing required fields, or validation failure
- `408 Request Timeout`: Socket timeout during request receive
- `500 Internal Server Error`: Server-side errors (memory allocation, NVS errors)

### Error Response Format

```json
{
  "status": "error",
  "message": "Descriptive error message"
}
```

### Validation Rules

1. **WiFi Configuration**:
   - SSID: Required, max 32 characters
   - Password: Optional, max 64 characters

2. **Email Configuration**:
   - SMTP server: Required, max 64 characters
   - SMTP port: Required, range 1-65535
   - Username: Required, max 64 characters
   - Password: Required, max 64 characters
   - Recipient: Required, must contain '@', max 64 characters

3. **JSON Parsing**:
   - All POST requests must contain valid JSON
   - Required fields must be present
   - String fields must not be null

## Testing Strategy

### Unit Testing Approach

Since this is embedded firmware, testing will focus on:

1. **Manual Testing via Web Interface**:
   - Test each endpoint with valid data
   - Test error cases (invalid JSON, missing fields)
   - Verify data persistence across reboots

2. **cURL Testing**:
   - GET requests to verify response format
   - POST requests with valid payloads
   - POST requests with invalid payloads
   - Verify HTTP status codes

3. **Integration Testing**:
   - Verify WiFi config endpoint works with existing wifi_manager
   - Verify network IP endpoint returns correct data when connected
   - Verify email config persists to NVS correctly
   - Verify OTA version endpoint returns valid firmware info
   - Verify system info endpoint returns accurate chip information

### Test Cases

1. **WiFi Config GET**: Verify returns current SSID
2. **WiFi Config POST**: Verify saves configuration
3. **WiFi Config POST Invalid**: Verify returns 400 for invalid JSON
4. **Network IP GET**: Verify returns IP info when connected
5. **Network IP GET Disconnected**: Verify handles disconnected state
6. **Email Config GET**: Verify returns config without password
7. **Email Config POST**: Verify saves valid configuration
8. **Email Config POST Invalid**: Verify validates email format
9. **OTA Version GET**: Verify returns firmware version info
10. **System Info GET**: Verify returns chip and system information

## Implementation Notes

### Memory Considerations

- Use stack allocation for small buffers (< 1KB)
- Use heap allocation for large JSON responses
- Free all allocated memory before returning from handlers
- Follow existing patterns in web_server.c for memory management

### Security Considerations

- Never return passwords in GET responses
- Validate all input data before processing
- Use secure storage (NVS) for sensitive data
- Consider adding authentication for configuration endpoints (future enhancement)

### Compatibility

- Maintain compatibility with existing web interface
- Follow existing JSON response formats
- Use existing NVS namespaces where applicable
- Leverage existing utility functions (wifi_manager, ntp_sync)

## Dependencies

### Existing Components

- `wifi_manager.h`: WiFi configuration and status
- `esp_netif.h`: Network interface information
- `esp_ota_ops.h`: OTA partition and app information
- `esp_chip_info.h`: Chip information
- `esp_system.h`: System information
- `nvs_flash.h`: Non-volatile storage
- `cJSON.h`: JSON parsing and generation

### New Files

No new files required. All implementations will be added to existing `web_server.c`.

## Future Enhancements

1. **Email Notification Implementation**: Actually send emails on doorbell events
2. **OTA Update Endpoint**: Add POST endpoint to trigger OTA updates
3. **Authentication**: Add API key or token-based authentication
4. **Rate Limiting**: Prevent abuse of configuration endpoints
5. **Configuration Backup/Restore**: Export/import all settings as JSON
