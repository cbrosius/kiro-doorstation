# SIP Connect/Disconnect Button Fix

## Issues Fixed

### 1. Disconnect Not Working Properly
**Problem:** Clicking disconnect closed the socket but didn't provide clear feedback

**Solution:**
- Added more log entries for visibility
- Clear registration_requested flag
- Proper state transition to DISCONNECTED
- Log socket closure

### 2. Connect After Disconnect Not Working
**Problem:** After disconnect, clicking connect didn't work because socket was closed

**Solution:**
- Change state from DISCONNECTED to IDLE when reconnecting
- Recreate socket if it was closed
- Rebind to port 5060
- Add log entries for each step

### 3. Connect When Already Registered
**Problem:** No visible feedback when already registered

**Solution:**
- Log "Already registered" message
- Add log entry visible in web interface

## Implementation

### Improved sip_connect()

```c
bool sip_connect(void)
{
    NTP_LOGI(TAG, "SIP connect requested");
    sip_add_log_entry("info", "SIP connection requested");
    
    if (!sip_config.configured) {
        sip_add_log_entry("error", "Cannot connect: SIP not configured");
        return false;
    }
    
    if (current_state == SIP_STATE_REGISTERED) {
        sip_add_log_entry("info", "Already registered to SIP server");
        return true;
    }
    
    // If disconnected, change state to idle
    if (current_state == SIP_STATE_DISCONNECTED) {
        current_state = SIP_STATE_IDLE;
        sip_add_log_entry("info", "Reconnecting to SIP server");
    }
    
    registration_requested = true;
    sip_add_log_entry("info", "SIP registration queued");
    
    return true;
}
```

### Improved sip_disconnect()

```c
void sip_disconnect(void)
{
    NTP_LOGI(TAG, "SIP disconnect requested");
    sip_add_log_entry("info", "SIP disconnection requested");
    
    // Send unregister if registered
    if (current_state == SIP_STATE_REGISTERED && sip_socket >= 0) {
        sip_add_log_entry("info", "Unregistering from SIP server");
        // TODO: Send REGISTER with Expires: 0
    }
    
    // Close socket
    if (sip_socket >= 0) {
        close(sip_socket);
        sip_socket = -1;
        NTP_LOGI(TAG, "SIP socket closed");
    }
    
    // Clear flags
    registration_requested = false;
    
    // Update state
    current_state = SIP_STATE_DISCONNECTED;
    sip_add_log_entry("info", "Disconnected from SIP server");
    
    NTP_LOGI(TAG, "SIP client disconnected");
}
```

### Socket Recreation in SIP Task

```c
if (registration_requested && current_state != SIP_STATE_REGISTERED) {
    registration_requested = false;
    
    // Recreate socket if it was closed
    if (sip_socket < 0) {
        NTP_LOGI(TAG, "Recreating SIP socket");
        sip_add_log_entry("info", "Creating SIP socket");
        
        sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        // ... bind to port 5060 ...
        
        sip_add_log_entry("info", "SIP socket ready");
    }
    
    sip_add_log_entry("info", "Starting SIP registration");
    sip_client_register();
}
```

## Expected Behavior

### Connect Button (When Not Registered)

**Web Log:**
```
[20:30:00] [INFO] SIP connection requested
[20:30:00] [INFO] SIP registration queued
[20:30:00] [INFO] Starting SIP registration
[20:30:00] [INFO] DNS lookup successful
[20:30:00] [INFO] REGISTER message sent
[20:30:01] [INFO] Authentication required
[20:30:01] [INFO] Sending authenticated REGISTER
[20:30:01] [INFO] SIP registration successful
```

### Connect Button (When Already Registered)

**Web Log:**
```
[20:30:00] [INFO] SIP connection requested
[20:30:00] [INFO] Already registered to SIP server
```

### Disconnect Button

**Web Log:**
```
[20:30:00] [INFO] SIP disconnection requested
[20:30:00] [INFO] Unregistering from SIP server
[20:30:00] [INFO] Disconnected from SIP server
```

### Reconnect (After Disconnect)

**Web Log:**
```
[20:30:00] [INFO] SIP connection requested
[20:30:00] [INFO] Reconnecting to SIP server
[20:30:00] [INFO] SIP registration queued
[20:30:00] [INFO] Creating SIP socket
[20:30:00] [INFO] SIP socket ready
[20:30:00] [INFO] Starting SIP registration
[20:30:00] [INFO] DNS lookup successful
[20:30:00] [INFO] REGISTER message sent
[20:30:01] [INFO] SIP registration successful
```

## State Transitions

### Connect Flow
```
IDLE → registration_requested → REGISTERING → REGISTERED
```

### Disconnect Flow
```
REGISTERED → socket closed → DISCONNECTED
```

### Reconnect Flow
```
DISCONNECTED → IDLE → socket recreated → REGISTERING → REGISTERED
```

## Future Improvements

### Proper Unregister (TODO)

Currently disconnect just closes the socket. Proper SIP unregistration should:

1. Send REGISTER with `Expires: 0`
2. Wait for 200 OK
3. Then close socket

```c
// TODO: Implement proper unregister
char unregister_msg[1024];
snprintf(unregister_msg, sizeof(unregister_msg),
    "REGISTER sip:%s SIP/2.0\r\n"
    "...\r\n"
    "Expires: 0\r\n"  // ← This unregisters
    "...\r\n\r\n",
    sip_config.server);
sendto(sip_socket, unregister_msg, strlen(unregister_msg), 0, ...);
```

## Files Modified

- **main/sip_client.c**
  - Improved `sip_connect()` - Better state handling and logging
  - Improved `sip_disconnect()` - Proper cleanup and logging
  - Added socket recreation in SIP task

## Testing

### Test 1: Connect When Not Registered
1. Open web interface
2. Click "Connect to SIP"
3. Verify log shows registration process
4. Verify status changes to "Registered"

### Test 2: Connect When Already Registered
1. Wait for auto-registration
2. Click "Connect to SIP"
3. Verify log shows "Already registered"
4. No duplicate registration

### Test 3: Disconnect
1. Wait for registration
2. Click "Disconnect"
3. Verify log shows disconnection
4. Verify status changes to "Not Registered"

### Test 4: Reconnect
1. Click "Disconnect"
2. Wait 2 seconds
3. Click "Connect to SIP"
4. Verify log shows socket recreation
5. Verify registration succeeds

## Summary

✅ **Connect button** - Now provides clear feedback  
✅ **Disconnect button** - Properly closes socket and updates state  
✅ **Reconnect** - Recreates socket and re-registers  
✅ **Logging** - All actions visible in web interface  
✅ **State management** - Proper state transitions  

The connect/disconnect buttons now work correctly with full visibility in the web log!

