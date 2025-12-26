# SIP Digest Authentication - TODO

## Current Status

The ESP32 can now:
✅ Bind to port 5060
✅ Send REGISTER messages
✅ Receive responses from SIP server
✅ Parse SIP messages correctly

However, the SIP server requires **Digest Authentication**, which is not yet implemented.

## What Happens Now

```
1. ESP32 sends REGISTER (no auth)
2. Server responds: 401 Unauthorized
   WWW-Authenticate: Digest realm="169.254.207.10", qop="auth", nonce="...", opaque=""
3. ESP32 logs: "Authentication required (not yet implemented)"
4. Registration fails
```

## What Needs to Be Implemented

### SIP Digest Authentication (RFC 2617)

When the server responds with 401, the ESP32 must:

1. **Parse WWW-Authenticate header**
   - Extract: realm, nonce, qop, opaque
   
2. **Calculate MD5 hashes**
   ```
   HA1 = MD5(username:realm:password)
   HA2 = MD5(method:uri)
   response = MD5(HA1:nonce:nc:cnonce:qop:HA2)
   ```

3. **Send authenticated REGISTER**
   ```
   REGISTER sip:192.168.60.90 SIP/2.0
   ...
   Authorization: Digest username="doorbell",
                         realm="169.254.207.10",
                         nonce="3401500490df776df16633d5b2f480ef",
                         uri="sip:192.168.60.90",
                         response="calculated_hash",
                         qop=auth,
                         nc=00000001,
                         cnonce="random_string",
                         opaque=""
   ...
   ```

4. **Receive 200 OK**
   - Registration successful!

## Implementation Steps

### 1. Add MD5 Library

ESP-IDF includes mbedTLS which has MD5:

```c
#include "mbedtls/md5.h"

void calculate_md5(const char* input, char* output) {
    unsigned char hash[16];
    mbedtls_md5_ret((unsigned char*)input, strlen(input), hash);
    
    // Convert to hex string
    for (int i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[32] = '\0';
}
```

### 2. Parse WWW-Authenticate Header

```c
typedef struct {
    char realm[128];
    char nonce[128];
    char qop[32];
    char opaque[128];
    bool valid;
} sip_auth_challenge_t;

sip_auth_challenge_t parse_www_authenticate(const char* header) {
    sip_auth_challenge_t challenge = {0};
    
    // Extract realm="..."
    char* realm_start = strstr(header, "realm=\"");
    if (realm_start) {
        realm_start += 7;  // Skip 'realm="'
        char* realm_end = strchr(realm_start, '"');
        if (realm_end) {
            int len = realm_end - realm_start;
            strncpy(challenge.realm, realm_start, len);
            challenge.realm[len] = '\0';
        }
    }
    
    // Extract nonce="..."
    char* nonce_start = strstr(header, "nonce=\"");
    if (nonce_start) {
        nonce_start += 7;
        char* nonce_end = strchr(nonce_start, '"');
        if (nonce_end) {
            int len = nonce_end - nonce_start;
            strncpy(challenge.nonce, nonce_start, len);
            challenge.nonce[len] = '\0';
        }
    }
    
    // Extract qop="..."
    char* qop_start = strstr(header, "qop=\"");
    if (qop_start) {
        qop_start += 5;
        char* qop_end = strchr(qop_start, '"');
        if (qop_end) {
            int len = qop_end - qop_start;
            strncpy(challenge.qop, qop_start, len);
            challenge.qop[len] = '\0';
        }
    }
    
    // Extract opaque="..."
    char* opaque_start = strstr(header, "opaque=\"");
    if (opaque_start) {
        opaque_start += 8;
        char* opaque_end = strchr(opaque_start, '"');
        if (opaque_end) {
            int len = opaque_end - opaque_start;
            strncpy(challenge.opaque, opaque_start, len);
            challenge.opaque[len] = '\0';
        }
    }
    
    challenge.valid = (strlen(challenge.realm) > 0 && strlen(challenge.nonce) > 0);
    return challenge;
}
```

### 3. Calculate Digest Response

```c
void calculate_digest_response(
    const char* username,
    const char* password,
    const char* realm,
    const char* nonce,
    const char* method,
    const char* uri,
    const char* qop,
    const char* nc,
    const char* cnonce,
    char* response_out)
{
    char ha1[33], ha2[33];
    char ha1_input[256], ha2_input[256], response_input[512];
    
    // Calculate HA1 = MD5(username:realm:password)
    snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
    calculate_md5(ha1_input, ha1);
    
    // Calculate HA2 = MD5(method:uri)
    snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, uri);
    calculate_md5(ha2_input, ha2);
    
    // Calculate response = MD5(HA1:nonce:nc:cnonce:qop:HA2)
    if (qop && strlen(qop) > 0) {
        snprintf(response_input, sizeof(response_input), 
                 "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
    } else {
        snprintf(response_input, sizeof(response_input), 
                 "%s:%s:%s", ha1, nonce, ha2);
    }
    
    calculate_md5(response_input, response_out);
}
```

### 4. Generate cnonce

```c
void generate_cnonce(char* cnonce_out, size_t len) {
    // Generate random string
    uint32_t random = esp_random();
    snprintf(cnonce_out, len, "%08x", random);
}
```

### 5. Build Authenticated REGISTER

```c
bool sip_client_register_with_auth(sip_auth_challenge_t* challenge) {
    char response[33];
    char cnonce[16];
    const char* nc = "00000001";
    
    generate_cnonce(cnonce, sizeof(cnonce));
    
    calculate_digest_response(
        sip_config.username,
        sip_config.password,
        challenge->realm,
        challenge->nonce,
        "REGISTER",
        sip_config.server,
        challenge->qop,
        nc,
        cnonce,
        response
    );
    
    // Build REGISTER with Authorization header
    char register_msg[2048];
    snprintf(register_msg, sizeof(register_msg),
        "REGISTER sip:%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%d\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %d@%s\r\n"
        "CSeq: 2 REGISTER\r\n"  // Increment CSeq!
        "Contact: <sip:%s@%s:5060>\r\n"
        "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
        "uri=\"sip:%s\", response=\"%s\", qop=%s, nc=%s, cnonce=\"%s\"%s%s\r\n"
        "Expires: 3600\r\n"
        "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
        "User-Agent: ESP32-Doorbell/1.0\r\n"
        "Content-Length: 0\r\n\r\n",
        // ... parameters ...
        sip_config.username, challenge->realm, challenge->nonce,
        sip_config.server, response, challenge->qop, nc, cnonce,
        strlen(challenge->opaque) > 0 ? ", opaque=\"" : "",
        strlen(challenge->opaque) > 0 ? challenge->opaque : ""
    );
    
    // Send message
    // ...
}
```

### 6. Update Message Handler

```c
// In sip_task(), when receiving 401:
else if (strstr(buffer, "SIP/2.0 401 Unauthorized")) {
    if (current_state == SIP_STATE_REGISTERING) {
        // Parse WWW-Authenticate header
        char* auth_header = strstr(buffer, "WWW-Authenticate:");
        if (auth_header) {
            sip_auth_challenge_t challenge = parse_www_authenticate(auth_header);
            if (challenge.valid) {
                NTP_LOGI(TAG, "Sending authenticated REGISTER");
                sip_client_register_with_auth(&challenge);
            }
        }
    }
}
```

## Testing Without Authentication

For testing without implementing auth, you can:

### Option 1: Disable Authentication on SIP Server
Configure your SIP server to allow registration without authentication (not recommended for production).

### Option 2: Use a Test SIP Server
Use a public test SIP server that doesn't require authentication:
- sip2sip.info (free SIP service)
- ekiga.net (free SIP service)

### Option 3: Implement Basic Auth First
Some servers support simpler authentication methods.

## Current Workaround

The code now correctly identifies the 401 response and logs:
```
"Authentication required (not yet implemented)"
```

This prevents the false "Incoming call" detection and clearly indicates what needs to be done.

## Priority

**Medium Priority** - The basic SIP infrastructure is working. Authentication is needed for production use with most SIP servers, but the system can be tested with:
- Servers that don't require auth
- Direct SIP calls (if server allows)
- Local testing

## Estimated Implementation Time

- MD5 calculation: 1 hour
- Header parsing: 2 hours
- Digest calculation: 2 hours
- Integration & testing: 3 hours
- **Total: ~8 hours**

## References

- RFC 2617: HTTP Authentication (Digest)
- RFC 3261: SIP (Section 22.4 - Digest Authentication)
- mbedTLS MD5 documentation

## Summary

✅ **SIP communication working** - Can send/receive messages  
✅ **401 correctly detected** - No longer misidentified as call  
❌ **Digest auth not implemented** - Needed for most SIP servers  
📋 **Clear implementation path** - Documented above  

The ESP32 SIP client is functional but needs digest authentication for production use with authenticated SIP servers.

