# Version Requirements

## ESP-IDF Version

**Required: ESP-IDF v5.5.1 or later**

### Why v5.5.1?

This project uses features and APIs that require ESP-IDF v5.5.1 or newer:

- **mbedTLS integration** - MD5 hashing for SIP digest authentication
- **SNTP improvements** - Better NTP time synchronization
- **WiFi stability** - Enhanced dual-core WiFi handling
- **HTTP server** - Modern esp_http_server API
- **Task watchdog** - Improved watchdog timer management

### Checking Your Version

```bash
idf.py --version
```

Expected output:
```
ESP-IDF v5.5.1 or later
```

### Installing ESP-IDF v5.5.1

#### Linux/macOS

```bash
# Clone ESP-IDF
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# Install
./install.sh esp32,esp32s3

# Setup environment
. ./export.sh
```

#### Windows

```powershell
# Clone ESP-IDF
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# Install
install.bat esp32,esp32s3

# Setup environment
export.bat
```

### Official Documentation

https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/

## Component Versions

### Required Components

| Component | Version | Purpose |
|-----------|---------|---------|
| esp_wifi | v5.5.1+ | WiFi connectivity |
| esp_http_server | v5.5.1+ | Web interface |
| esp_netif | v5.5.1+ | Network interface |
| nvs_flash | v5.5.1+ | Configuration storage |
| mbedtls | v5.5.1+ | MD5 for SIP auth |
| json (cJSON) | v5.5.1+ | JSON parsing |
| esp_sntp | v5.5.1+ | NTP time sync |

All components are included with ESP-IDF v5.5.1.

## Hardware Support

### Tested Platforms

✅ **ESP32-S3** - Fully tested and working  
✅ **ESP32** - Should work (not extensively tested)  

### Minimum Requirements

- **Flash**: 4 MB (recommended)
- **RAM**: 520 KB (ESP32-S3 has 512 KB SRAM + 8 KB RTC)
- **Cores**: 2 (dual-core required for WiFi isolation)

## Firmware Version

**Current Version: 1.0.0**

### Changelog

#### v1.0.0 (2025-10-15)
- ✅ SIP client with digest authentication (MD5)
- ✅ NTP time synchronization with timezone support
- ✅ Web interface with real-time logs
- ✅ WiFi management with network scanning
- ✅ Dual-core architecture (SIP on Core 1, WiFi on Core 0)
- ✅ Socket binding to port 5060
- ✅ Automatic registration with 5-second delay
- ✅ DTMF decoder (hardware not yet connected)
- ✅ GPIO handlers for doorbells and relays
- ✅ I2S audio handlers (hardware not yet connected)

## Compatibility Matrix

| ESP-IDF Version | Status | Notes |
|-----------------|--------|-------|
| v5.5.1 | ✅ Tested | Recommended |
| v5.5.0 | ⚠️ May work | Not tested |
| v5.4.x | ❌ Not supported | Missing features |
| v5.3.x | ❌ Not supported | API incompatibilities |
| v5.2.x | ❌ Not supported | Too old |

## Upgrading from Older ESP-IDF

If you have an older ESP-IDF version:

1. **Backup your current installation**
2. **Install ESP-IDF v5.5.1** (see above)
3. **Clean build directory**
   ```bash
   idf.py fullclean
   ```
4. **Rebuild project**
   ```bash
   idf.py build
   ```

## Troubleshooting

### "mbedtls/md5.h: No such file or directory"

**Cause:** ESP-IDF version too old or mbedtls not linked

**Solution:**
1. Upgrade to ESP-IDF v5.5.1
2. Ensure `mbedtls` is in CMakeLists.txt REQUIRES

### "esp_sntp.h: No such file or directory"

**Cause:** ESP-IDF version too old

**Solution:** Upgrade to ESP-IDF v5.5.1

### Build Errors After Upgrade

**Solution:**
```bash
idf.py fullclean
idf.py build
```

## Summary

✅ **ESP-IDF v5.5.1 or later required**  
✅ **All dependencies included with ESP-IDF**  
✅ **Tested on ESP32-S3**  
✅ **No external libraries needed**  

For installation help, see: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32/get-started/

