# API Endpoints Testing Guide

## New Endpoints Implemented

All 5 missing endpoints have been implemented and are ready for testing:

### 1. WiFi Configuration
```bash
# GET current WiFi config
curl http://<device-ip>/api/wifi/config

# POST new WiFi config (does not auto-connect)
curl -X POST http://<device-ip>/api/wifi/config \
  -H "Content-Type: application/json" \
  -d '{"ssid":"MyNetwork","password":"mypassword"}'
```

### 2. Network IP Information
```bash
# GET network IP details
curl http://<device-ip>/api/network/ip
```

Expected response:
```json
{
  "connected": true,
  "ip_address": "192.168.1.100",
  "netmask": "255.255.255.0",
  "gateway": "192.168.1.1",
  "dns": "8.8.8.8"
}
```

### 3. Email Configuration
```bash
# GET email config (password not included)
curl http://<device-ip>/api/email/config

# POST email config
curl -X POST http://<device-ip>/api/email/config \
  -H "Content-Type: application/json" \
  -d '{
    "smtp_server":"smtp.gmail.com",
    "smtp_port":587,
    "smtp_username":"doorbell@example.com",
    "smtp_password":"secret",
    "recipient_email":"admin@example.com",
    "enabled":true
  }'
```

### 4. OTA Version Information
```bash
# GET firmware version info
curl http://<device-ip>/api/ota/version
```

Expected response:
```json
{
  "version": "v1.0.0",
  "project_name": "sip_doorbell",
  "build_date": "Oct 17 2025",
  "build_time": "14:30:00",
  "idf_version": "v5.5.1",
  "ota_partition_size": 1572864,
  "ota_available_space": 1048576
}
```

### 5. System Information
```bash
# GET comprehensive system info
curl http://<device-ip>/api/system/info
```

Expected response:
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

## Testing Checklist

### Basic Functionality
- [ ] All endpoints return HTTP 200 OK
- [ ] All endpoints return valid JSON
- [ ] No 404 errors in browser console

### WiFi Configuration
- [ ] GET returns current SSID
- [ ] POST saves configuration to NVS
- [ ] POST with invalid JSON returns 400
- [ ] Password is never returned in GET response

### Network IP
- [ ] Returns correct IP when connected
- [ ] Returns empty values when disconnected
- [ ] Includes netmask, gateway, and DNS

### Email Configuration
- [ ] GET returns config without password
- [ ] POST validates email format (must contain @)
- [ ] POST validates port range (1-65535)
- [ ] Configuration persists across reboots

### OTA Version
- [ ] Returns build date and time
- [ ] Returns IDF version
- [ ] Returns partition information

### System Info
- [ ] Returns correct chip model (ESP32-S3)
- [ ] Returns valid MAC address
- [ ] Returns current heap and uptime
- [ ] Values update on subsequent requests

## Error Testing

### Invalid JSON
```bash
curl -X POST http://<device-ip>/api/wifi/config \
  -H "Content-Type: application/json" \
  -d 'invalid json'
```
Expected: HTTP 400 Bad Request

### Missing Required Fields
```bash
curl -X POST http://<device-ip>/api/email/config \
  -H "Content-Type: application/json" \
  -d '{"smtp_server":"smtp.gmail.com"}'
```
Expected: Partial update (only provided fields updated)

### Invalid Email Format
```bash
curl -X POST http://<device-ip>/api/email/config \
  -H "Content-Type: application/json" \
  -d '{"recipient_email":"notanemail"}'
```
Expected: HTTP 400 Bad Request

## Web Interface Testing

The web interface should now:
1. No longer show 404 errors for these endpoints
2. Display system information correctly
3. Allow WiFi configuration management
4. Show network details
5. Allow email notification setup
6. Display firmware version

## Notes

- WiFi config POST does not automatically connect - use `/api/wifi/connect` for that
- Email functionality is configuration only - actual email sending not implemented yet
- System info uses hardcoded values for chip specs (accurate for ESP32-S3)
- OTA version uses compile-time macros for build info
