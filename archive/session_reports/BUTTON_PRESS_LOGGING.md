# Button Press Logging

## Feature Added

Button presses are now logged in the web interface to distinguish user-initiated actions from automatic actions (like auto-registration).

## Implementation

### New Function: addLocalLogEntry()

```javascript
function addLocalLogEntry(type, message) {
    const logDiv = document.getElementById('sip-log');
    const entryDiv = document.createElement('div');
    
    // Color code by type
    let color = '#d4d4d4';
    if (type === 'error') color = '#f48771';
    else if (type === 'info') color = '#4ec9b0';
    else if (type === 'user') color = '#c586c0'; // Purple for user actions
    
    // Format current time
    const now = new Date();
    const timeStr = now.toLocaleString('en-GB', { 
        year: 'numeric', 
        month: '2-digit', 
        day: '2-digit',
        hour: '2-digit', 
        minute: '2-digit', 
        second: '2-digit',
        hour12: false 
    });
    
    entryDiv.innerHTML = `<span style="color: #888;">[${timeStr}]</span> <span style="color: ${color};">[${type.toUpperCase()}]</span> ${escapeHtml(message)}`;
    logDiv.appendChild(entryDiv);
    logDiv.scrollTop = logDiv.scrollHeight;
}
```

### Updated Button Handlers

**Connect Button:**
```javascript
async function connectToSip() {
    // Log button press
    addLocalLogEntry('user', '🔘 Connect button pressed');
    
    // ... rest of function
}
```

**Disconnect Button:**
```javascript
async function disconnectFromSip() {
    // Log button press
    addLocalLogEntry('user', '🔘 Disconnect button pressed');
    
    // ... rest of function
}
```

## Log Display

### User Actions (Purple)

Button presses are shown in **purple** color to distinguish them from system messages:

```
[15/10/2025, 20:30:00] [USER] 🔘 Connect button pressed
[15/10/2025, 20:30:00] [INFO] SIP connection requested
[15/10/2025, 20:30:00] [INFO] SIP registration queued
```

### Auto-Registration (Cyan)

Automatic actions remain in **cyan** color:

```
[15/10/2025, 20:21:40] [INFO] Auto-registration triggered after 5000 ms delay
[15/10/2025, 20:21:40] [INFO] Processing registration request
[15/10/2025, 20:21:40] [INFO] SIP registration with 192.168.60.90
```

## Color Scheme

| Type | Color | Hex | Usage |
|------|-------|-----|-------|
| ERROR | Red | #f48771 | Error messages |
| INFO | Cyan | #4ec9b0 | System info |
| SENT | Yellow | #dcdcaa | Sent SIP messages |
| RECEIVED | Blue | #9cdcfe | Received SIP messages |
| **USER** | **Purple** | **#c586c0** | **User actions** |

## Example Scenarios

### Scenario 1: User Clicks Connect

```
[15/10/2025, 20:30:00] [USER] 🔘 Connect button pressed
[15/10/2025, 20:30:00] [INFO] SIP connection requested
[15/10/2025, 20:30:00] [INFO] SIP registration queued
[15/10/2025, 20:30:00] [INFO] Creating SIP socket
[15/10/2025, 20:30:00] [INFO] SIP socket ready
[15/10/2025, 20:30:00] [INFO] Starting SIP registration
[15/10/2025, 20:30:00] [INFO] DNS lookup successful
[15/10/2025, 20:30:00] [INFO] REGISTER message sent
[15/10/2025, 20:30:01] [INFO] Authentication required
[15/10/2025, 20:30:01] [INFO] Sending authenticated REGISTER
[15/10/2025, 20:30:01] [INFO] SIP registration successful
```

### Scenario 2: Auto-Registration on Boot

```
[15/10/2025, 20:21:40] [INFO] Auto-registration triggered after 5000 ms delay
[15/10/2025, 20:21:40] [INFO] Processing registration request
[15/10/2025, 20:21:40] [INFO] SIP registration with 192.168.60.90
[15/10/2025, 20:21:40] [INFO] DNS lookup successful
[15/10/2025, 20:21:40] [INFO] REGISTER message sent
[15/10/2025, 20:21:41] [INFO] SIP registration successful
```

### Scenario 3: User Clicks Disconnect

```
[15/10/2025, 20:35:00] [USER] 🔘 Disconnect button pressed
[15/10/2025, 20:35:00] [INFO] SIP disconnection requested
[15/10/2025, 20:35:00] [INFO] Unregistering from SIP server
[15/10/2025, 20:35:00] [INFO] Disconnected from SIP server
```

### Scenario 4: User Reconnects

```
[15/10/2025, 20:36:00] [USER] 🔘 Connect button pressed
[15/10/2025, 20:36:00] [INFO] SIP connection requested
[15/10/2025, 20:36:00] [INFO] Reconnecting to SIP server
[15/10/2025, 20:36:00] [INFO] SIP registration queued
[15/10/2025, 20:36:00] [INFO] Creating SIP socket
[15/10/2025, 20:36:00] [INFO] SIP socket ready
[15/10/2025, 20:36:00] [INFO] Starting SIP registration
[15/10/2025, 20:36:01] [INFO] SIP registration successful
```

## Benefits

### 1. Clear Action Source
Easy to see if action was:
- User-initiated (button press)
- System-initiated (auto-registration)

### 2. Debugging
Helps troubleshoot issues:
- "Did the user click the button?"
- "Was it auto-registration or manual?"
- "When exactly did the user take action?"

### 3. User Feedback
Immediate visual confirmation:
- Button press is logged instantly
- User knows action was received
- Clear timeline of events

### 4. Audit Trail
Complete history of:
- User actions
- System responses
- Timing of events

## Technical Details

### Client-Side Logging

Button press logs are added **immediately** on the client side:
- No server round-trip needed
- Instant feedback
- Uses browser's local time

### Server-Side Logging

Backend actions are logged separately:
- Fetched via API polling
- Uses NTP-synchronized time
- Includes all SIP protocol details

### Time Synchronization

- **User actions**: Browser local time
- **System actions**: NTP-synchronized time
- Both formatted consistently for easy correlation

## Files Modified

- **main/index.html**
  - Added `addLocalLogEntry()` function
  - Updated `connectToSip()` to log button press
  - Updated `disconnectFromSip()` to log button press
  - Added purple color for USER type

## Summary

✅ **Button presses logged** - Immediate visual feedback  
✅ **Purple color** - Easy to distinguish from system messages  
✅ **Emoji indicator** - 🔘 shows user action  
✅ **Clear timeline** - User actions vs system actions  
✅ **Better debugging** - Know exactly what triggered each action  

Now you can easily see whether SIP actions were triggered by:
- User clicking buttons (purple, with 🔘)
- Auto-registration (cyan, no emoji)
- Other system events

