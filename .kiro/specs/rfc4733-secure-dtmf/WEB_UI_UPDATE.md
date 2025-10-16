# Web Interface Update for DTMF Security

## Changes Made

I've updated the `main/index.html` file to add the DTMF Security configuration interface that was missing.

### New Sections Added:

1. **DTMF Security Configuration** - Form to configure:
   - PIN enabled/disabled toggle
   - PIN code (1-8 digits)
   - Command timeout (5000-30000 ms)
   - Max failed attempts (1-10)

2. **DTMF Security Logs** - Real-time log viewer showing:
   - Successful/failed command attempts
   - Timestamps (NTP or uptime)
   - Command details
   - Caller information
   - Failure reasons

### JavaScript Functions Added:

- `fetchDtmfSecurityConfig()` - Loads current configuration from API
- `refreshDtmfLogs()` - Fetches and displays security logs
- `clearDtmfLogDisplay()` - Clears the log display
- Auto-refresh every 2 seconds for logs
- Form submission handler for saving configuration

### API Endpoints Used:

- `GET /api/dtmf/security` - Get current configuration
- `POST /api/dtmf/security` - Update configuration
- `GET /api/dtmf/logs` - Get security logs

---

## Next Steps: Build and Flash

To use the updated web interface, you need to rebuild and flash the firmware:

### Option 1: Using ESP-IDF (if installed)

```bash
# Navigate to project directory
cd /path/to/project

# Build the firmware
idf.py build

# Flash to ESP32
idf.py flash

# Monitor serial output (optional)
idf.py monitor
```

### Option 2: Using Docker/DevContainer

If you're using the devcontainer:

```bash
# Inside the container
idf.py build
idf.py flash monitor
```

### Option 3: Pre-built Binary (if available)

If you have a pre-built binary in the `firmware/` directory:

```bash
# Flash using esptool
esptool.py --chip esp32s3 --port COM3 write_flash 0x0 firmware/sip_doorbell.bin
```

---

## Verification After Flashing

1. **Power cycle the ESP32** (unplug and replug)
2. **Wait for WiFi connection** (check serial console or router)
3. **Open web interface**: `http://[doorstation-ip]`
4. **Scroll down** - You should now see:
   - "DTMF Security Configuration" section
   - "DTMF Security Logs" section

---

## Testing the Web Interface

### Step 1: Verify Configuration Section Appears

You should see a form with:
- [ ] Checkbox: "Enable PIN Protection"
- [ ] Input: "PIN Code (1-8 digits)"
- [ ] Input: "Command Timeout (ms)"
- [ ] Input: "Max Failed Attempts"
- [ ] Button: "Save Security Configuration"

### Step 2: Configure Initial Settings

1. Check "Enable PIN Protection"
2. Enter PIN: `1234`
3. Set Timeout: `10000`
4. Set Max Attempts: `3`
5. Click "Save Security Configuration"

**Expected**: Green toast message "DTMF security configuration saved!"

### Step 3: Verify Configuration Saved

Refresh the page and check that:
- Status shows: "PIN Enabled: ✅ Yes"
- Current PIN shows: "****"
- Timeout shows: "10000 ms"
- Max Attempts shows: "3"

### Step 4: Test API Directly (Optional)

```bash
# Get configuration
curl http://[doorstation-ip]/api/dtmf/security

# Expected response:
{
  "pin_enabled": true,
  "pin_code": "1234",
  "timeout_ms": 10000,
  "max_attempts": 3
}
```

---

## Troubleshooting

### Issue: DTMF Security section still not visible

**Possible causes:**
1. Firmware not flashed yet
2. Browser cache showing old HTML
3. Wrong IP address

**Solutions:**
1. Flash the updated firmware
2. Hard refresh browser: `Ctrl+F5` (Windows) or `Cmd+Shift+R` (Mac)
3. Clear browser cache
4. Try incognito/private browsing mode
5. Verify IP address is correct

### Issue: Configuration doesn't save

**Check:**
1. Serial console for error messages
2. Network connectivity
3. API endpoint is responding: `curl http://[ip]/api/dtmf/security`

### Issue: Logs not appearing

**Check:**
1. Make a test call and send DTMF commands
2. Check that auto-refresh is working (should update every 2 seconds)
3. Click "Refresh Logs" button manually
4. Check API: `curl http://[ip]/api/dtmf/logs`

---

## Once Web Interface is Working

You can proceed with **Test 8.2: Command Execution with PIN**:

1. ✅ Configure PIN via web interface (as described above)
2. Make a test call to the doorstation
3. Press DTMF: `*` `1` `2` `3` `4` `#`
4. Verify door opener activates
5. Check security logs in web interface

---

## File Modified

- `main/index.html` - Added DTMF Security UI sections and JavaScript functions

The backend API endpoints were already implemented in `main/web_server.c`, so no changes were needed there.
