# Changelog - ESP32 SIP Door Station

## Version 1.0.7 - 2025-10-15

### ✅ Stability Confirmed + Logging Added

#### WiFi Stability Verified
- ✅ **Confirmed**: WiFi remains stable with dual-core SIP
- ✅ **No beacon timeouts** with SIP running on Core 1
- ✅ **Architecture validated** - complete isolation works

#### Added: SIP Connection Logging
- ✅ Thread-safe log buffer (50 entries, 256 bytes each)
- ✅ Logs SIP messages (sent/received/info/error)
- ✅ Timestamp for each entry
- ✅ Web interface integration ready
- ✅ Minimal memory footprint (~13KB)

#### Stack Size Fix
- ✅ Increased SIP task stack from 4KB to 8KB
- ✅ Prevents stack overflow during DNS resolution
- ✅ Sufficient for SIP message processing

**Changes:**
- Added `sip_add_log_entry()` for thread-safe logging
- Added `sip_get_log_entries()` for web interface
- Logging integrated into SIP message handling
- Mutex-protected log buffer

**Memory Usage:**
- Log buffer: 50 × 280 bytes = 14KB
- Stack: 8KB per task
- Total: ~22KB additional RAM

---

## Version 1.0.6 - 2025-10-15

### 🚀 Major Architecture Improvement

#### Implemented: Dual-Core SIP Architecture
- ✅ **New**: SIP task now runs on Core 1 (APP CPU)
- ✅ **Benefit**: Complete isolation from WiFi on Core 0 (PRO CPU)
- ✅ **Result**: Zero interference between SIP and WiFi

**Architecture:**
```
Core 0 (PRO CPU):  WiFi + Network + HTTP + System
Core 1 (APP CPU):  SIP Client (isolated)
```

**Implementation Details:**
- SIP task pinned to Core 1 using `xTaskCreatePinnedToCore()`
- Task priority set to 3 (low, won't interfere with system tasks)
- Polling delay increased to 200ms (minimal CPU usage ~1-2%)
- Added core ID logging for verification

**Benefits:**
- ✅ WiFi has exclusive access to Core 0
- ✅ No CPU competition between WiFi and SIP
- ✅ No task starvation possible
- ✅ Better overall performance
- ✅ Scalable architecture for future features

**Testing:**
- WiFi remains stable with SIP running
- No beacon timeouts
- SIP functionality unchanged
- Efficient resource utilization

**Documentation:**
- Added `docs/DUAL_CORE_ARCHITECTURE.md`
- Comprehensive dual-core design guide
- Best practices and troubleshooting
- Performance monitoring guidelines

---

## Version 1.0.0 - 2025-10-15

### 🎉 Initial Release

#### Core Features
- ESP32-S3 based SIP door station
- Dual doorbell buttons (GPIO 21, 22)
- I2S audio support
- DTMF tone detection
- Relay control for door and light
- Web interface for configuration
- WiFi management with AP mode
- SIP client with registration
- Real-time status monitoring

#### Hardware Support
- ESP32-S3 DevKit
- I2S audio codec (MAX98357A, INMP441)
- GPIO relay control
- Push button inputs

#### Development
- ESP-IDF v5.5.1
- VS Code devcontainer
- CMake build system
- Comprehensive documentation

---

**Note**: This version implements proper dual-core architecture to ensure WiFi stability while running SIP client.
