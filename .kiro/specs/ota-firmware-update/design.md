# OTA Firmware Update - Design Document

## Overview

This feature implements Over-The-Air (OTA) firmware updates for the ESP32 SIP Door Station, allowing administrators to update device firmware remotely through the web interface without physical access. The implementation uses ESP-IDF's native OTA partition system with dual app partitions for safe rollback capability.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Web Interface  │
│   (Browser)     │
└────────┬────────┘
         │ HTTP POST /api/ota/upload
         │ (multipart/form-data)
         ▼
┌─────────────────┐
│  Web Server     │
│  (ESP32 HTTP)   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  OTA Handler    │
│  - Validation   │
│  - Flash Write  │
│  - Verification │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  ESP-IDF OTA    │
│  - Partition    │
│  - Flash Driver │
└─────────────────┘
```

### Partition Layout

The ESP32 uses a dual OTA partition scheme:
- **factory** (optional): Factory firmware
- **ota_0**: First application partition
- **ota_1**: Second application partition
- **otadata**: OTA data partition (tracks active partition)

Current partition configuration in `partitions.csv`:
```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
ota_0,    app,  ota_0,   ,        1M,
ota_1,    app,  ota_1,   ,        1M,
```

## Components and Interfaces

### 1. OTA Handler Module (`ota_handler.c/h`)

**Purpose**: Manages the OTA update process including validation, flashing, and rollback.

**Public API**:
```c
// Initialize OTA handler
void ota_handler_init(void);

// Get current firmware information
typedef struct {
    char version[32];
    char build_date[32];
    char idf_version[32];
    uint32_t app_size;
    const char* partition_label;
    bool can_rollback;
} ota_info_t;

void ota_get_info(ota_info_t* info);

// Start OTA update process
esp_err_t ota_begin_update(size_t image_size);

// Write firmware chunk
esp_err_t ota_write_chunk(const uint8_t* data, size_t length);

// Finalize and validate update
esp_err_t ota_end_update(void);

// Abort update process
void ota_abort_update(void);

// Rollback to previous firmware
esp_err_t ota_rollback(void);

// Mark current firmware as valid (prevent auto-rollback)
esp_err_t ota_mark_valid(void);
```

**Internal State Machine**:
```
IDLE → BEGIN → WRITING → VALIDATING → COMPLETE
  ↓      ↓        ↓           ↓
  └──────┴────────┴───────────┴──→ ABORT
```

### 2. Web Server OTA Endpoints

**Endpoint**: `POST /api/ota/upload`
- **Content-Type**: `multipart/form-data`
- **Field**: `firmware` (binary file)
- **Response**: JSON with progress/status

**Endpoint**: `GET /api/ota/info`
- **Response**: Current firmware information

**Endpoint**: `POST /api/ota/rollback`
- **Response**: Rollback status

**Endpoint**: `GET /api/ota/status`
- **Response**: Current OTA operation status

### 3. Web Interface Components

**OTA Update Section** (in Dashboard or System menu):
- Current firmware version display
- Build date and ESP-IDF version
- File upload interface with drag-and-drop
- Progress bar with percentage
- Status messages (uploading, verifying, applying)
- Rollback button (if previous firmware available)

## Data Models

### OTA State Structure
```c
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_BEGIN,
    OTA_STATE_WRITING,
    OTA_STATE_VALIDATING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ABORT,
    OTA_STATE_ERROR
} ota_state_t;

typedef struct {
    ota_state_t state;
    size_t total_size;
    size_t written_size;
    uint8_t progress_percent;
    char error_message[128];
    esp_ota_handle_t update_handle;
    const esp_partition_t* update_partition;
    uint32_t start_time;
} ota_context_t;
```

### Firmware Metadata
```c
typedef struct {
    uint32_t magic;              // 0xE9 (ESP32 app magic)
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed_size;
    uint32_t entry_addr;
    // ... ESP32 image header fields
} esp_image_header_t;
```

## Error Handling

### Validation Checks
1. **File Format Validation**
   - Check ESP32 image magic number (0xE9)
   - Verify image header structure
   - Validate segment count and sizes

2. **Size Validation**
   - Ensure firmware size fits in OTA partition
   - Check available flash space
   - Verify image is not truncated

3. **Integrity Validation**
   - Calculate and verify SHA256 checksum
   - Validate image segments
   - Check for corruption during transfer

4. **Compatibility Validation**
   - Verify chip type matches (ESP32-S3)
   - Check minimum ESP-IDF version compatibility
   - Validate partition table compatibility

### Error Recovery
- **Upload Failure**: Abort OTA, clean up partition, return to IDLE
- **Validation Failure**: Abort OTA, preserve current firmware
- **Flash Write Failure**: Abort OTA, mark partition as invalid
- **Power Loss During Update**: Auto-rollback on next boot (ESP-IDF feature)
- **Boot Failure After Update**: Auto-rollback after 3 failed boots

### Error Messages
```c
#define OTA_ERR_INVALID_IMAGE    "Invalid firmware image format"
#define OTA_ERR_SIZE_MISMATCH    "Firmware size exceeds partition capacity"
#define OTA_ERR_FLASH_WRITE      "Flash write error"
#define OTA_ERR_VALIDATION       "Firmware validation failed"
#define OTA_ERR_NO_PARTITION     "No available OTA partition"
#define OTA_ERR_IN_PROGRESS      "OTA update already in progress"
```

## Testing Strategy

### Unit Tests
1. **OTA Handler Tests**
   - Test state machine transitions
   - Validate chunk writing logic
   - Test error handling paths
   - Verify rollback functionality

2. **Validation Tests**
   - Test image header parsing
   - Verify checksum calculation
   - Test size validation
   - Test format validation

### Integration Tests
1. **Web Interface Tests**
   - Upload valid firmware file
   - Upload invalid file (should reject)
   - Upload oversized file (should reject)
   - Test progress reporting
   - Test rollback functionality

2. **End-to-End Tests**
   - Complete update cycle with valid firmware
   - Verify device reboots and runs new firmware
   - Test rollback to previous version
   - Test auto-rollback on boot failure

### Safety Tests
1. **Power Loss Simulation**
   - Interrupt update at various stages
   - Verify device boots with previous firmware
   - Ensure no bricking scenarios

2. **Corrupted Firmware Tests**
   - Upload firmware with corrupted segments
   - Verify validation catches corruption
   - Ensure device remains bootable

3. **Stress Tests**
   - Multiple consecutive updates
   - Large firmware files (near partition limit)
   - Concurrent web interface operations

## Security Considerations

### Current Implementation (Phase 1)
- No authentication (web interface is open)
- No encryption (HTTP only)
- No firmware signing verification

### Future Enhancements (Phase 2+)
- Add web interface authentication (username/password)
- Implement HTTPS for encrypted upload
- Add firmware signing and signature verification
- Implement secure boot chain
- Add update authorization tokens

## Performance Considerations

### Upload Speed
- Expected: 50-100 KB/s over WiFi
- Typical 1MB firmware: 10-20 seconds upload time
- Flash write speed: ~100 KB/s

### Memory Usage
- OTA context: ~200 bytes
- Write buffer: 4KB (configurable)
- HTTP receive buffer: 4KB
- Total additional heap: ~10KB during update

### Flash Wear
- Flash endurance: ~100,000 write cycles
- OTA updates: ~1000 bytes per sector
- Expected lifetime: >10,000 updates per sector

## Implementation Notes

### ESP-IDF OTA API Usage
```c
// Get update partition
const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

// Begin OTA
esp_ota_handle_t update_handle;
esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);

// Write data
esp_ota_write(update_handle, data, length);

// Finalize
esp_ota_end(update_handle);

// Set boot partition
esp_ota_set_boot_partition(update_partition);

// Restart
esp_restart();
```

### Rollback Mechanism
```c
// Check if rollback is possible
const esp_partition_t* running = esp_ota_get_running_partition();
const esp_partition_t* last_valid = esp_ota_get_last_invalid_partition();

// Perform rollback
esp_ota_set_boot_partition(last_valid);
esp_restart();
```

### Progress Reporting
- Report progress every 10% or 50KB (whichever is smaller)
- Use chunked transfer encoding for real-time updates
- Implement timeout (5 minutes max for upload)

## Web Interface Design

### Upload Flow
1. User selects firmware file (.bin)
2. JavaScript validates file extension
3. Display file size and estimated time
4. User clicks "Upload and Update"
5. Confirmation dialog with warnings
6. Upload begins with progress bar
7. Server validates and flashes firmware
8. Success message with countdown to reboot
9. Device reboots automatically
10. User reconnects to verify new version

### UI Elements
```html
<div class="ota-section">
  <h3>Firmware Update</h3>
  <div class="current-firmware">
    <p>Current Version: <span id="fw-version">1.0.0</span></p>
    <p>Build Date: <span id="fw-date">2025-01-15</span></p>
    <p>ESP-IDF: <span id="idf-version">v5.5.1</span></p>
  </div>
  
  <div class="upload-area">
    <input type="file" id="firmware-file" accept=".bin">
    <button onclick="uploadFirmware()">Upload and Update</button>
  </div>
  
  <div class="progress-container" style="display:none;">
    <div class="progress-bar">
      <div class="progress-fill" id="ota-progress"></div>
    </div>
    <p id="ota-status">Uploading...</p>
  </div>
  
  <div class="rollback-section">
    <button onclick="rollbackFirmware()" id="rollback-btn" disabled>
      Rollback to Previous Version
    </button>
  </div>
</div>
```

### JavaScript Upload Handler
```javascript
async function uploadFirmware() {
  const fileInput = document.getElementById('firmware-file');
  const file = fileInput.files[0];
  
  if (!file) {
    alert('Please select a firmware file');
    return;
  }
  
  if (!file.name.endsWith('.bin')) {
    alert('Invalid file type. Please select a .bin file');
    return;
  }
  
  if (!confirm('This will update the firmware and restart the device. Continue?')) {
    return;
  }
  
  const formData = new FormData();
  formData.append('firmware', file);
  
  const progressContainer = document.querySelector('.progress-container');
  const progressFill = document.getElementById('ota-progress');
  const statusText = document.getElementById('ota-status');
  
  progressContainer.style.display = 'block';
  
  try {
    const xhr = new XMLHttpRequest();
    
    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const percent = (e.loaded / e.total) * 100;
        progressFill.style.width = percent + '%';
        statusText.textContent = `Uploading: ${percent.toFixed(0)}%`;
      }
    });
    
    xhr.addEventListener('load', () => {
      if (xhr.status === 200) {
        statusText.textContent = 'Update successful! Device will restart...';
        setTimeout(() => {
          window.location.reload();
        }, 5000);
      } else {
        statusText.textContent = 'Update failed: ' + xhr.responseText;
      }
    });
    
    xhr.addEventListener('error', () => {
      statusText.textContent = 'Upload error. Please try again.';
    });
    
    xhr.open('POST', '/api/ota/upload');
    xhr.send(formData);
    
  } catch (error) {
    statusText.textContent = 'Error: ' + error.message;
  }
}
```

## Dependencies

### ESP-IDF Components
- `esp_ota_ops`: OTA operations API
- `esp_partition`: Partition table access
- `esp_app_format`: Image format validation
- `esp_https_ota`: HTTPS OTA (future)
- `bootloader_support`: Bootloader utilities

### Project Components
- `web_server`: HTTP server for upload endpoint
- `nvs_flash`: Store OTA metadata
- `wifi_manager`: Ensure WiFi stability during update

## Rollout Plan

### Phase 1: Basic OTA (MVP)
- Implement OTA handler module
- Add web server upload endpoint
- Create web interface upload section
- Basic validation and error handling
- Manual reboot after update

### Phase 2: Enhanced Safety
- Add automatic rollback on boot failure
- Implement firmware signing verification
- Add progress reporting improvements
- Enhanced validation checks

### Phase 3: Advanced Features
- Add HTTPS support for secure upload
- Implement web authentication
- Add update scheduling
- Remote update from URL
- Update notifications

## References

- [ESP-IDF OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [ESP32 App Image Format](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/app_image_format.html)
- [Partition Tables](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html)
