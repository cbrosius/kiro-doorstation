# Configuration Backup/Restore - Design Document

## Overview

This feature enables system administrators to backup and restore all device configuration settings through the web interface. It provides a safety mechanism for configuration migration between devices, recovery from configuration errors, and maintaining configuration snapshots for disaster recovery.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Web Interface  │
│   (Browser)     │
└────────┬────────┘
         │ GET /api/config/backup (download JSON)
         │ POST /api/config/restore (upload JSON)
         ▼
┌─────────────────┐
│  Web Server     │
│  Config API     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Config Manager  │
│  - Serialize    │
│  - Deserialize  │
│  - Validate     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  NVS Storage    │
│  (Flash)        │
└─────────────────┘
```

### Data Flow

**Backup Flow**:
```
NVS → Config Manager → JSON Serialization → HTTP Response → Browser Download
```

**Restore Flow**:
```
Browser Upload → HTTP Request → JSON Parsing → Validation → Config Manager → NVS
```

## Components and Interfaces

### 1. Configuration Manager Module (`config_manager.c/h`)

**Purpose**: Centralized configuration management with backup/restore capabilities.

**Public API**:
```c
// Initialize configuration manager
void config_manager_init(void);

// Backup configuration to JSON string
esp_err_t config_backup_to_json(char** json_output, size_t* json_size);

// Restore configuration from JSON string
esp_err_t config_restore_from_json(const char* json_input, size_t json_size);

// Validate configuration JSON
esp_err_t config_validate_json(const char* json_input, size_t json_size);

// Get configuration metadata
typedef struct {
    char firmware_version[32];
    char backup_timestamp[32];
    uint32_t config_version;
    size_t config_size;
} config_metadata_t;

esp_err_t config_get_metadata(config_metadata_t* metadata);

// Factory reset (clear all configuration)
esp_err_t config_factory_reset(void);
```

### 2. Configuration Structure

**Complete Configuration Model**:
```c
typedef struct {
    // Metadata
    uint32_t version;           // Config format version
    char timestamp[32];         // Backup creation time
    char firmware_version[32];  // Firmware version at backup time
    
    // WiFi Configuration
    struct {
        char ssid[32];
        char password[64];
        bool use_static_ip;
        char static_ip[16];
        char subnet_mask[16];
        char gateway[16];
        char dns1[16];
        char dns2[16];
    } wifi;
    
    // SIP Configuration
    struct {
        char server[64];
        int port;
        char username[32];
        char password[64];      // Optional: can be excluded
        char apartment1_uri[64];
        char apartment2_uri[64];
    } sip;
    
    // NTP Configuration
    struct {
        char server[64];
        char timezone[64];
    } ntp;
    
    // DTMF Security Configuration
    struct {
        bool pin_enabled;
        char pin_code[9];       // Optional: can be excluded
        uint32_t timeout_seconds;
        uint32_t max_attempts;
    } dtmf_security;
    
    // Hardware Configuration (future)
    struct {
        uint8_t bell1_gpio;
        uint8_t bell2_gpio;
        uint8_t door_relay_gpio;
        uint8_t light_relay_gpio;
    } hardware;
    
} device_config_t;
```

### 3. Web Server Endpoints

**Endpoint**: `GET /api/config/backup`
- **Query Params**: `?include_passwords=true|false` (default: false)
- **Response**: JSON file download with `Content-Disposition: attachment`
- **Filename**: `doorstation-config-YYYY-MM-DD-HHMMSS.json`

**Endpoint**: `POST /api/config/restore`
- **Content-Type**: `application/json`
- **Body**: Configuration JSON
- **Response**: Validation result and restore status

**Endpoint**: `POST /api/config/validate`
- **Content-Type**: `application/json`
- **Body**: Configuration JSON
- **Response**: Validation result with detailed errors

**Endpoint**: `POST /api/config/factory-reset`
- **Body**: `{"confirm": true}`
- **Response**: Reset status

### 4. Web Interface Components

**Configuration Backup/Restore Section** (in Dashboard or System menu):
- Current configuration summary
- Backup button with password inclusion option
- Restore file upload interface
- Configuration preview before restore
- Factory reset button with confirmation

## Data Models

### JSON Configuration Format

```json
{
  "version": 1,
  "timestamp": "2025-10-17T10:30:00Z",
  "firmware_version": "1.2.0",
  "wifi": {
    "ssid": "MyNetwork",
    "password": "********",
    "use_static_ip": false,
    "static_ip": "",
    "subnet_mask": "",
    "gateway": "",
    "dns1": "",
    "dns2": ""
  },
  "sip": {
    "server": "sip.example.com",
    "port": 5060,
    "username": "doorstation",
    "password": "********",
    "apartment1_uri": "sip:apt1@example.com",
    "apartment2_uri": "sip:apt2@example.com"
  },
  "ntp": {
    "server": "pool.ntp.org",
    "timezone": "Europe/Berlin"
  },
  "dtmf_security": {
    "pin_enabled": true,
    "pin_code": "********",
    "timeout_seconds": 10,
    "max_attempts": 3
  },
  "hardware": {
    "bell1_gpio": 21,
    "bell2_gpio": 22,
    "door_relay_gpio": 25,
    "light_relay_gpio": 26
  }
}
```

### Password Handling Options

**Option 1: Exclude Passwords (Default)**
```json
{
  "wifi": {
    "password": null
  },
  "sip": {
    "password": null
  }
}
```

**Option 2: Include Passwords (User Choice)**
```json
{
  "wifi": {
    "password": "actual_password"
  },
  "sip": {
    "password": "actual_password"
  }
}
```

**Option 3: Encrypted Passwords (Future)**
```json
{
  "wifi": {
    "password_encrypted": "base64_encrypted_data"
  }
}
```

## Error Handling

### Validation Checks

1. **JSON Format Validation**
   - Valid JSON syntax
   - Required fields present
   - Correct data types

2. **Version Compatibility**
   - Check config version matches supported versions
   - Warn if firmware version mismatch
   - Reject incompatible configurations

3. **Field Validation**
   - SSID length (1-32 characters)
   - IP address format validation
   - Port number range (1-65535)
   - PIN code format (1-8 digits)
   - Timezone string validation

4. **Logical Validation**
   - If static IP enabled, all IP fields must be valid
   - SIP server and username required if SIP enabled
   - NTP server required if NTP enabled

### Error Messages

```c
#define CONFIG_ERR_INVALID_JSON      "Invalid JSON format"
#define CONFIG_ERR_MISSING_FIELD     "Required field missing: %s"
#define CONFIG_ERR_INVALID_VERSION   "Unsupported configuration version"
#define CONFIG_ERR_INVALID_IP        "Invalid IP address format"
#define CONFIG_ERR_INVALID_PORT      "Port must be between 1 and 65535"
#define CONFIG_ERR_INVALID_PIN       "PIN must be 1-8 digits"
#define CONFIG_ERR_FIRMWARE_MISMATCH "Configuration from different firmware version"
#define CONFIG_ERR_RESTORE_FAILED    "Failed to restore configuration"
```

### Error Recovery

- **Invalid JSON**: Reject restore, display detailed error
- **Missing Passwords**: Prompt user to enter passwords manually
- **Validation Failure**: Show validation errors, allow correction
- **Restore Failure**: Rollback to previous configuration
- **Partial Restore**: Apply valid sections, report failures

## Testing Strategy

### Unit Tests
1. **JSON Serialization Tests**
   - Test backup with all fields populated
   - Test backup with optional fields empty
   - Test password exclusion/inclusion
   - Verify JSON format correctness

2. **JSON Deserialization Tests**
   - Test restore with valid JSON
   - Test restore with missing optional fields
   - Test restore with invalid data types
   - Test version compatibility

3. **Validation Tests**
   - Test IP address validation
   - Test port number validation
   - Test PIN code validation
   - Test required field validation

### Integration Tests
1. **Backup/Restore Cycle**
   - Backup configuration
   - Modify configuration
   - Restore from backup
   - Verify all settings restored correctly

2. **Cross-Device Migration**
   - Backup from device A
   - Restore to device B
   - Verify functionality on device B

3. **Password Handling**
   - Backup without passwords
   - Restore and verify passwords not overwritten
   - Backup with passwords
   - Restore and verify passwords restored

### Safety Tests
1. **Corruption Handling**
   - Test with corrupted JSON
   - Test with truncated file
   - Verify no configuration damage

2. **Rollback Tests**
   - Trigger restore failure mid-process
   - Verify rollback to previous config
   - Ensure device remains functional

## Security Considerations

### Current Implementation (Phase 1)
- Passwords excluded by default
- Plain text JSON (no encryption)
- No authentication required for backup/restore
- Warning displayed when including passwords

### Future Enhancements (Phase 2+)
- Encrypt sensitive fields in backup
- Add web interface authentication
- Implement backup encryption with user password
- Add audit logging for backup/restore operations
- Implement backup signing for integrity verification

## Performance Considerations

### Memory Usage
- Configuration structure: ~1KB
- JSON serialization buffer: ~4KB
- Temporary parsing buffer: ~4KB
- Total peak usage: ~10KB

### Backup/Restore Time
- Backup generation: <100ms
- JSON download: <1 second
- Restore parsing: <200ms
- NVS write: <500ms
- Total restore time: <1 second

### File Size
- Typical backup size: 1-2KB
- With passwords: 1.5-2.5KB
- Compressed (future): <1KB

## Implementation Notes

### JSON Library Selection

Use **cJSON** (already included in ESP-IDF):
```c
#include "cJSON.h"

// Create JSON object
cJSON* root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "version", "1");
cJSON_AddStringToObject(root, "timestamp", timestamp);

// Serialize to string
char* json_string = cJSON_Print(root);

// Parse JSON string
cJSON* root = cJSON_Parse(json_string);
const char* version = cJSON_GetObjectItem(root, "version")->valuestring;

// Cleanup
cJSON_Delete(root);
free(json_string);
```

### NVS Access Pattern

```c
// Read all configuration from NVS
esp_err_t config_read_all(device_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    
    // Read WiFi config
    size_t len = sizeof(config->wifi.ssid);
    nvs_get_str(handle, "wifi_ssid", config->wifi.ssid, &len);
    
    // Read SIP config
    len = sizeof(config->sip.server);
    nvs_get_str(handle, "sip_server", config->sip.server, &len);
    
    // ... read all other fields
    
    nvs_close(handle);
    return ESP_OK;
}

// Write all configuration to NVS
esp_err_t config_write_all(const device_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    
    // Write WiFi config
    nvs_set_str(handle, "wifi_ssid", config->wifi.ssid);
    
    // Write SIP config
    nvs_set_str(handle, "sip_server", config->sip.server);
    
    // ... write all other fields
    
    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}
```

### Configuration Rollback

```c
// Backup current config before restore
static device_config_t rollback_config;

esp_err_t config_restore_from_json(const char* json_input, size_t json_size) {
    // Backup current configuration
    config_read_all(&rollback_config);
    
    // Parse and validate new configuration
    device_config_t new_config;
    esp_err_t err = parse_json_to_config(json_input, &new_config);
    if (err != ESP_OK) {
        return err;
    }
    
    // Apply new configuration
    err = config_write_all(&new_config);
    if (err != ESP_OK) {
        // Rollback on failure
        config_write_all(&rollback_config);
        return err;
    }
    
    return ESP_OK;
}
```

## Web Interface Design

### Backup/Restore Section UI

```html
<div class="config-backup-section">
  <h3>Configuration Backup & Restore</h3>
  
  <div class="config-summary">
    <h4>Current Configuration</h4>
    <p>Last Modified: <span id="config-last-modified">2025-10-17 10:30:00</span></p>
    <p>Firmware Version: <span id="config-fw-version">1.2.0</span></p>
  </div>
  
  <div class="backup-controls">
    <h4>Backup Configuration</h4>
    <label>
      <input type="checkbox" id="include-passwords">
      Include passwords in backup (not recommended for sharing)
    </label>
    <button onclick="backupConfiguration()" class="action-btn">
      📥 Download Backup
    </button>
  </div>
  
  <div class="restore-controls">
    <h4>Restore Configuration</h4>
    <input type="file" id="restore-file" accept=".json">
    <button onclick="previewRestore()" class="action-btn">
      📤 Preview Restore
    </button>
  </div>
  
  <div id="restore-preview" class="preview-area" style="display:none;">
    <h4>Configuration Preview</h4>
    <pre id="preview-content"></pre>
    <div class="preview-actions">
      <button onclick="applyRestore()" class="action-btn">Apply Configuration</button>
      <button onclick="cancelRestore()" class="secondary-btn">Cancel</button>
    </div>
  </div>
  
  <div class="factory-reset-controls">
    <h4>Factory Reset</h4>
    <p class="warning">⚠️ This will erase all configuration and restart the device.</p>
    <button onclick="factoryReset()" class="danger-btn">Factory Reset</button>
  </div>
  
  <div id="config-feedback" class="feedback-area"></div>
</div>
```

### JavaScript Functions

```javascript
async function backupConfiguration() {
  const includePasswords = document.getElementById('include-passwords').checked;
  
  try {
    const response = await fetch(`/api/config/backup?include_passwords=${includePasswords}`);
    const blob = await response.blob();
    
    // Generate filename with timestamp
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, -5);
    const filename = `doorstation-config-${timestamp}.json`;
    
    // Trigger download
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    window.URL.revokeObjectURL(url);
    
    showFeedback('success', 'Configuration backup downloaded');
    
  } catch (error) {
    showFeedback('error', 'Backup failed: ' + error.message);
  }
}

async function previewRestore() {
  const fileInput = document.getElementById('restore-file');
  const file = fileInput.files[0];
  
  if (!file) {
    alert('Please select a configuration file');
    return;
  }
  
  try {
    const text = await file.text();
    const config = JSON.parse(text);
    
    // Validate configuration
    const response = await fetch('/api/config/validate', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: text
    });
    
    const validation = await response.json();
    
    if (!validation.valid) {
      showFeedback('error', 'Invalid configuration: ' + validation.errors.join(', '));
      return;
    }
    
    // Show preview
    document.getElementById('preview-content').textContent = JSON.stringify(config, null, 2);
    document.getElementById('restore-preview').style.display = 'block';
    
    // Store for later restore
    window.restoreConfig = text;
    
  } catch (error) {
    showFeedback('error', 'Failed to read file: ' + error.message);
  }
}

async function applyRestore() {
  if (!confirm('This will replace all current configuration. Continue?')) {
    return;
  }
  
  try {
    const response = await fetch('/api/config/restore', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: window.restoreConfig
    });
    
    const result = await response.json();
    
    if (result.success) {
      showFeedback('success', 'Configuration restored. Device will restart...');
      setTimeout(() => {
        window.location.reload();
      }, 3000);
    } else {
      showFeedback('error', 'Restore failed: ' + result.message);
    }
    
  } catch (error) {
    showFeedback('error', 'Restore failed: ' + error.message);
  }
}

function cancelRestore() {
  document.getElementById('restore-preview').style.display = 'none';
  document.getElementById('restore-file').value = '';
  window.restoreConfig = null;
}

async function factoryReset() {
  const confirmation = prompt('Type "RESET" to confirm factory reset:');
  
  if (confirmation !== 'RESET') {
    return;
  }
  
  try {
    const response = await fetch('/api/config/factory-reset', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({confirm: true})
    });
    
    const result = await response.json();
    
    if (result.success) {
      alert('Factory reset complete. Device will restart...');
      setTimeout(() => {
        window.location.href = 'http://192.168.4.1';
      }, 3000);
    }
    
  } catch (error) {
    showFeedback('error', 'Factory reset failed: ' + error.message);
  }
}
```

## Dependencies

### ESP-IDF Components
- `nvs_flash`: Non-volatile storage
- `cJSON`: JSON parsing and generation
- `esp_system`: System information

### Project Components
- `wifi_manager`: WiFi configuration
- `sip_client`: SIP configuration
- `ntp_sync`: NTP configuration
- `dtmf_decoder`: DTMF security configuration
- `web_server`: HTTP endpoints

## Rollout Plan

### Phase 1: Basic Backup/Restore (MVP)
- Implement configuration manager module
- Add backup/restore endpoints
- Create web interface section
- Support password exclusion
- Basic validation

### Phase 2: Enhanced Safety
- Add configuration preview
- Implement rollback on failure
- Enhanced validation
- Better error messages

### Phase 3: Advanced Features
- Add backup encryption
- Implement scheduled backups
- Add backup history
- Remote backup to cloud/server
- Configuration diff viewer

## References

- [ESP-IDF NVS API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [cJSON Library](https://github.com/DaveGamble/cJSON)
- [JSON Schema Validation](https://json-schema.org/)
