# Configuration Change Logging Feature

## Overview

Added security logging for DTMF configuration changes so administrators can track when security settings are modified.

## Changes Made

### 1. Added New Command Type (`main/dtmf_decoder.h`)

Added `CMD_CONFIG_CHANGE` to the command type enumeration:

```c
typedef enum {
    CMD_DOOR_OPEN,      // *[PIN]# or *1# (legacy)
    CMD_LIGHT_TOGGLE,   // *2#
    CMD_CONFIG_CHANGE,  // Security configuration changed
    CMD_INVALID
} dtmf_command_type_t;
```

### 2. Log Configuration Changes (`main/dtmf_decoder.c`)

Updated `dtmf_save_security_config()` to create a security log entry when configuration is saved:

```c
// Create log entry for configuration change
char config_details[64];
snprintf(config_details, sizeof(config_details), 
         "PIN:%s timeout:%lums attempts:%d",
         config->pin_enabled ? "enabled" : "disabled",
         config->timeout_ms,
         config->max_attempts);

dtmf_add_security_log(CMD_CONFIG_CHANGE, true, "config_update", "web_interface", config_details);
```

**Log Entry Details:**
- **Type**: `CMD_CONFIG_CHANGE`
- **Success**: Always `true` (only successful saves are logged)
- **Command**: `"config_update"`
- **Caller**: `"web_interface"` (identifies source of change)
- **Reason**: Configuration details (e.g., `"PIN:enabled timeout:5000ms attempts:3"`)

### 3. API Response Mapping (`main/web_server.c`)

Updated `get_dtmf_logs_handler()` to map the new type to JSON:

```c
case CMD_CONFIG_CHANGE:
    type_str = "config_change";
    break;
```

### 4. Web Interface Display (`main/index.html`)

Enhanced log display to show configuration changes with distinct styling:

```javascript
// Color code and icon by type and success
let color, icon;
if (entry.type === 'config_change') {
    color = '#dcdcaa';  // Yellow for config changes
    icon = '⚙️';
} else {
    color = entry.success ? '#4ec9b0' : '#f48771';
    icon = entry.success ? '✅' : '❌';
}

// Format message based on type
if (entry.type === 'config_change') {
    message = `${icon} Configuration updated: ${entry.reason || entry.command}`;
    if (entry.caller) {
        message += ` by ${entry.caller}`;
    }
}
```

**Visual Appearance:**
- Icon: ⚙️ (gear emoji)
- Color: Yellow (`#dcdcaa`)
- Format: `⚙️ Configuration updated: PIN:enabled timeout:5000ms attempts:3 by web_interface`

## Example Log Entries

### Configuration Change
```json
{
  "timestamp": 1697462400000,
  "type": "config_change",
  "success": true,
  "command": "config_update",
  "caller": "web_interface",
  "reason": "PIN:enabled timeout:5000ms attempts:3"
}
```

### Door Opener Command
```json
{
  "timestamp": 1697462450000,
  "type": "door_open",
  "success": true,
  "command": "*[PIN]#",
  "caller": "sip:user@domain.com",
  "reason": ""
}
```

### Failed Attempt
```json
{
  "timestamp": 1697462500000,
  "type": "door_open",
  "success": false,
  "command": "*[PIN]#",
  "caller": "sip:unknown@domain.com",
  "reason": "invalid_pin"
}
```

## Testing

### Test 1: Change Configuration via Web Interface

1. Open web interface
2. Navigate to DTMF Security section
3. Change settings:
   - Enable PIN: ✅
   - PIN Code: `5678`
   - Timeout: `15000` ms
   - Max Attempts: `5`
4. Click "Save Security Configuration"

**Expected Result:**
- Green toast: "DTMF security configuration saved!"
- Security log shows: `⚙️ Configuration updated: PIN:enabled timeout:15000ms attempts:5 by web_interface`

### Test 2: Verify Log Persistence

1. Make configuration change
2. Refresh web page
3. Check security logs

**Expected Result:**
- Configuration change entry still visible
- Timestamp shows when change was made

### Test 3: Multiple Configuration Changes

1. Change PIN to `1111`, save
2. Change timeout to `20000`, save
3. Disable PIN, save

**Expected Result:**
- Three separate log entries
- Each shows the configuration at time of save
- Chronological order maintained

## Benefits

1. **Audit Trail**: Track who changed security settings and when
2. **Troubleshooting**: Identify when configuration changes may have caused issues
3. **Security**: Detect unauthorized configuration changes
4. **Compliance**: Maintain records of security policy modifications

## Log Entry Retention

- Configuration changes are stored in the same circular buffer as command logs
- Maximum 50 entries total (all types combined)
- Oldest entries are overwritten when buffer is full
- Logs persist until device reboot (not stored in NVS)

## Future Enhancements

Potential improvements:
1. **Caller identification**: Track which user/IP made the change
2. **Change details**: Show before/after values
3. **Persistent logging**: Store config changes in NVS for long-term audit
4. **Email notifications**: Alert on security configuration changes
5. **Rollback capability**: Undo recent configuration changes

## Files Modified

- `main/dtmf_decoder.h` - Added `CMD_CONFIG_CHANGE` enum value
- `main/dtmf_decoder.c` - Added logging in `dtmf_save_security_config()`, added forward declaration
- `main/web_server.c` - Added type mapping for `config_change`
- `main/index.html` - Enhanced display for configuration change events

## Summary

Configuration changes are now logged to the security log with:
- ⚙️ Distinct icon and yellow color
- Configuration details (PIN status, timeout, max attempts)
- Source identifier (web_interface)
- Timestamp (NTP or system time)

This provides a complete audit trail of both command executions and security configuration changes.
