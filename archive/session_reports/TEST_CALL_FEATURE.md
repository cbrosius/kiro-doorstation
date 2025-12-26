# Test Call Feature for SIP Targets

## Overview
Added the ability to initiate test calls directly from the web interface to verify that SIP targets are configured correctly. Each target (Target1 and Target2) has its own "Test Call" button.

## Implementation

### Backend Changes

#### 1. New API Endpoint (`main/web_server.c`)
- **Endpoint**: `POST /api/sip/testcall`
- **Handler**: `post_sip_test_call_handler()`
- **Request Body**: 
  ```json
  {
    "target": "sip:apartment1@domain.com"
  }
  ```
- **Response**:
  ```json
  {
    "status": "success|failed",
    "message": "Test call initiated | Error message"
  }
  ```

#### 2. Validation
- Checks if target parameter is provided and valid
- Verifies SIP client is registered before allowing test calls
- Returns appropriate error messages if validation fails

### Frontend Changes

#### 1. UI Layout (`main/index.html`)
Each SIP target input field now has an inline "Test Call" button:
```html
<label for="target1">SIP-Target1 (Apartment 1):</label>
<div style="display: flex; gap: 10px;">
    <input type="text" id="target1" name="target1" style="flex: 1;">
    <button type="button" onclick="testCallTarget(1)">Test Call</button>
</div>
```

#### 2. JavaScript Function
- **Function**: `testCallTarget(targetNumber)`
- **Parameters**: `1` for Target1, `2` for Target2
- **Behavior**:
  - Validates target field is not empty
  - Logs user action to SIP log
  - Sends POST request to `/api/sip/testcall`
  - Shows toast notification with result
  - Disables button during call initiation

## User Workflow

### Step-by-Step Usage
1. **Configure Targets**:
   - Enter SIP-Target1 (e.g., `sip:apartment1@domain.com`)
   - Enter SIP-Target2 (e.g., `sip:apartment2@domain.com`)
   - Fill in SIP Server, Username, and Password
   - Click "Save Configuration"

2. **Connect to SIP**:
   - Click "Connect to SIP" button
   - Wait for registration to complete
   - Status should show "Registered"

3. **Test Targets**:
   - Click "Test Call" button next to Target1
   - The configured device should ring
   - Monitor SIP log for call progress
   - Repeat for Target2

4. **Verify**:
   - Check that correct devices ring for each target
   - Review SIP log entries for any errors
   - Adjust configuration if needed

## Benefits

### 1. Configuration Verification
- Quickly verify targets are reachable before deploying
- Test without physical doorbell button presses
- Identify configuration errors immediately

### 2. Troubleshooting
- Isolate issues to specific targets
- Test after configuration changes
- Verify SIP server connectivity

### 3. User Experience
- No need to physically press doorbell buttons
- Instant feedback via toast notifications
- Clear error messages for common issues

## Error Handling

### Common Error Messages
1. **"Please enter SIP-Target first"**
   - Target field is empty
   - Solution: Enter a valid SIP URI

2. **"Not registered to SIP server. Please connect first."**
   - SIP client not connected
   - Solution: Click "Connect to SIP" button

3. **"Invalid or missing target"**
   - Target format is incorrect
   - Solution: Use format `sip:user@domain.com`

4. **"Error initiating test call"**
   - Network or server error
   - Solution: Check SIP log for details

## Technical Details

### Call Flow
1. User clicks "Test Call" button
2. JavaScript validates input and sends API request
3. Backend validates SIP registration status
4. Backend calls `sip_client_make_call(target)`
5. SIP client initiates INVITE to target
6. Response sent back to web interface
7. Toast notification shown to user
8. Call progress logged in SIP log

### Security Considerations
- Test calls require active SIP registration
- No authentication bypass for test calls
- Same security as physical doorbell presses
- All calls logged for audit trail

### Performance
- Non-blocking API call
- Button disabled during call initiation
- Timeout handled gracefully
- No impact on other system functions

## Testing Checklist

- [ ] Test Call button appears next to both targets
- [ ] Button disabled when target field is empty
- [ ] Error shown when not registered to SIP
- [ ] Test call initiates successfully when registered
- [ ] Toast notification shows success/failure
- [ ] SIP log shows test call entries
- [ ] Target device rings when test call made
- [ ] Button re-enables after call initiation
- [ ] Multiple test calls can be made sequentially
- [ ] Works for both Target1 and Target2

## Future Enhancements

Possible improvements:
1. Add call duration timer
2. Show call status (ringing, answered, ended)
3. Add "Hangup" button for active test calls
4. Display last test call result persistently
5. Add audio feedback when call connects
6. Support for video call testing (if hardware supports)
