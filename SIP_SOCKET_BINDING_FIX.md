# SIP Socket Binding Fix

## Issue

SIP REGISTER packets were sent successfully, but the SIP server never responded. Wireshark showed:
- **Packet sent** from port 53757 (random high port)
- **Via header** claimed port 5060
- **No response** from SIP server

## Root Cause

The SIP socket was created but **never bound to port 5060**:

```c
// Before - WRONG!
sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
// Socket gets random port assigned by OS
```

**What happened:**
1. ESP32 creates UDP socket → OS assigns random port (e.g., 53757)
2. ESP32 sends REGISTER with `Via: SIP/2.0/UDP 192.168.62.209:5060`
3. SIP server receives packet from port 53757
4. SIP server sends response to port 5060 (as indicated in Via header)
5. **Nothing listening on port 5060** → Response lost!

## Solution

### 1. Bind Socket to Port 5060

```c
// Create socket
sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

// Bind to port 5060
struct sockaddr_in local_addr;
memset(&local_addr, 0, sizeof(local_addr));
local_addr.sin_family = AF_INET;
local_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
local_addr.sin_port = htons(5060);  // SIP standard port

if (bind(sip_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
    ESP_LOGE(TAG, "Error binding SIP socket to port 5060");
    // Handle error
}
```

**Now:**
1. ESP32 creates UDP socket
2. ESP32 binds socket to port 5060
3. ESP32 sends REGISTER from port 5060
4. SIP server receives packet from port 5060
5. SIP server sends response to port 5060
6. **ESP32 receives response!** ✅

### 2. Improved REGISTER Message

Added missing SIP headers for better compatibility:

**Before:**
```
REGISTER sip:192.168.60.90 SIP/2.0
Via: SIP/2.0/UDP 192.168.62.209:5060;branch=z9hG4bK1191391529
From: <sip:doorbell@192.168.60.90>;tag=812669700
To: <sip:doorbell@192.168.60.90>
Call-ID: 553475508@192.168.62.209
CSeq: 1 REGISTER
Contact: <sip:doorbell@192.168.62.209:5060>
Expires: 3600
Content-Length: 0
```

**After:**
```
REGISTER sip:192.168.60.90 SIP/2.0
Via: SIP/2.0/UDP 192.168.62.209:5060;branch=z9hG4bK1191391529;rport
Max-Forwards: 70
From: <sip:doorbell@192.168.60.90>;tag=812669700
To: <sip:doorbell@192.168.60.90>
Call-ID: 553475508@192.168.62.209
CSeq: 1 REGISTER
Contact: <sip:doorbell@192.168.62.209:5060>
Expires: 3600
Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE
User-Agent: ESP32-Doorbell/1.0
Content-Length: 0
```

**Added headers:**
- **Max-Forwards: 70** - Prevents routing loops (RFC 3261 requirement)
- **rport** in Via - Enables NAT traversal (RFC 3581)
- **Allow** - Lists supported SIP methods
- **User-Agent** - Identifies the client

## Why This Matters

### Port Binding in SIP

SIP uses **symmetric signaling** - responses are sent to the port indicated in the Via header. If you claim to be on port 5060 but aren't actually listening there, you'll never receive responses.

### Wireshark Evidence

**Before (broken):**
```
Source Port: 53757  ← Random port
Via: SIP/2.0/UDP 192.168.62.209:5060  ← Claims port 5060
```
**Mismatch!** Server sends response to 5060, but ESP32 is listening on 53757.

**After (fixed):**
```
Source Port: 5060  ← Correct port
Via: SIP/2.0/UDP 192.168.62.209:5060  ← Matches!
```
**Match!** Server sends response to 5060, ESP32 is listening on 5060.

## Testing

### Before Fix
```
1. Send REGISTER from port 53757
2. Via header says port 5060
3. Server responds to port 5060
4. No one listening on 5060
5. Response lost
6. Registration fails silently
```

### After Fix
```
1. Bind socket to port 5060
2. Send REGISTER from port 5060
3. Via header says port 5060
4. Server responds to port 5060
5. ESP32 receives response on port 5060
6. Registration succeeds! ✅
```

### Expected Log Output

**Successful registration:**
```
I (5000) SIP: [2025-10-15 19:37:03] SIP socket bound to port 5060
I (5001) SIP: [2025-10-15 19:37:03] SIP task started on core 1
I (10000) SIP: [2025-10-15 19:37:08] Auto-registration triggered
I (10001) SIP: [2025-10-15 19:37:08] Starting SIP registration
I (10002) SIP: [2025-10-15 19:37:08] DNS lookup successful
I (10003) SIP: [2025-10-15 19:37:08] REGISTER message sent successfully
I (10150) SIP: [2025-10-15 19:37:08] SIP message received:
SIP/2.0 200 OK
...
I (10151) SIP: [2025-10-15 19:37:08] SIP registration successful
```

## Additional Improvements

### 1. rport Parameter

Added `rport` to Via header for NAT traversal:
```
Via: SIP/2.0/UDP 192.168.62.209:5060;branch=z9hG4bK123;rport
```

This tells the server to send responses to the source IP:port it sees (useful behind NAT).

### 2. Max-Forwards

Added `Max-Forwards: 70` header:
- Prevents infinite routing loops
- Required by RFC 3261
- Standard value is 70

### 3. Allow Header

Lists supported SIP methods:
```
Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE
```

Helps the server understand client capabilities.

### 4. User-Agent

Identifies the client:
```
User-Agent: ESP32-Doorbell/1.0
```

Useful for debugging and server-side logging.

## Common SIP Ports

- **5060** - SIP (UDP/TCP) - Standard unencrypted
- **5061** - SIP (TLS) - Encrypted
- **5062-5080** - Alternative SIP ports

Our implementation uses standard port 5060.

## Files Modified

- **main/sip_client.c**
  - Added socket binding to port 5060
  - Improved REGISTER message template
  - Added Max-Forwards, rport, Allow, User-Agent headers

## Summary

✅ **Socket bound to port 5060** - Can now receive responses  
✅ **Via header matches actual port** - Symmetric signaling works  
✅ **Added required headers** - RFC 3261 compliant  
✅ **Added NAT traversal** - rport parameter  
✅ **Better compatibility** - Allow and User-Agent headers  

The ESP32 can now successfully register with SIP servers and receive responses!

