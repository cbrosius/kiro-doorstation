# Doorstation SIP Requirements & Implementation Plan

**Document Version:** 1.0  
**Date:** 2025-10-26  
**Use Case:** ESP32 Doorbell/Intercom System

---

## Executive Summary

This document identifies **essential vs optional** RFC 3261 features for a doorstation/intercom system and provides an implementation plan for graceful handling of unsupported features.

**Goal:** Ensure the doorstation works reliably with common SIP servers (Fritz!Box, Asterisk, FreeSWITCH) while gracefully rejecting unsupported features.

---

## 1. ESSENTIAL FEATURES (Already Implemented) ✅

These features are **required** for basic doorstation operation and are already implemented:

### 1.1 Core Call Flow
- ✅ **REGISTER** - Register with SIP server (lines 1412-1499)
- ✅ **INVITE (UAS)** - Receive incoming calls (line 1022)
- ✅ **INVITE (UAC)** - Make outgoing calls (line 1644)
- ✅ **ACK** - Acknowledge call setup (line 697)
- ✅ **BYE (send/receive)** - End calls (lines 1209, 1928)
- ✅ **200 OK** - Success responses (lines 632, 638, 651)
- ✅ **180 Ringing** - Call progress (line 758)
- ✅ **401 Unauthorized** - Authentication challenges (line 767)

### 1.2 Authentication
- ✅ **Digest Authentication** - MD5 with qop=auth (lines 190-298)

### 1.3 Media
- ✅ **SDP (basic)** - Audio session description (lines 1131, 1700)
- ✅ **RTP** - Audio transmission (via rtp_handler)

### 1.4 Core Headers
- ✅ Via, From, To, Call-ID, CSeq, Contact, Content-Type, Content-Length

**Status:** All essential features work. Doorstation can register, receive calls, make calls, and handle audio.

---

## 2. OPTIONAL FEATURES (Need Graceful Handling) ⚠️

These features are **not essential** for doorstation operation but may be requested by some SIP servers. They should be handled gracefully with "not implemented" responses.

### 2.1 Methods to Handle with Dummy Responses

| Method | Current Status | Server Use Case | Required Response | Implementation Priority |
|--------|----------------|-----------------|-------------------|------------------------|
| **OPTIONS** | Not handled | Capability query, keepalive | 200 OK with Allow header | HIGH |
| **INFO** | Not handled | Mid-call signaling, DTMF relay | 200 OK or 501 Not Implemented | MEDIUM |
| **UPDATE** | Not handled | Session modification | 501 Not Implemented | MEDIUM |
| **PRACK** | Not handled | Reliable provisional responses | 501 Not Implemented | LOW |
| **SUBSCRIBE** | Not handled | Event notifications | 501 Not Implemented | LOW |
| **NOTIFY** | Not handled | Event delivery | 481 Call Does Not Exist | LOW |
| **MESSAGE** | Not handled | Instant messaging | 501 Not Implemented | LOW |
| **REFER** | Not handled | Call transfer | 501 Not Implemented | LOW |
| **CANCEL** (receive) | Not handled | Call cancellation by caller | 200 OK to CANCEL + 487 to INVITE | HIGH |

### 2.2 Response Codes to Add

**Essential Error Responses:**
- **400 Bad Request** - Malformed SIP messages
- **405 Method Not Allowed** - Unsupported methods
- **415 Unsupported Media Type** - Unsupported SDP
- **481 Call/Transaction Does Not Exist** - Unknown call reference
- **488 Not Acceptable Here** - Unsupported codec/media
- **501 Not Implemented** - Unsupported feature
- **503 Service Unavailable** - Temporary overload

### 2.3 Headers That May Be Received (Safe to Ignore)

These headers may appear in requests but can be safely ignored for basic doorstation operation:

- **Supported** - Feature tags (can ignore if we don't support them)
- **Require** - Must reject request if we don't support required features
- **Proxy-Require** - Proxy-specific requirements (can ignore as endpoint)
- **Route/Record-Route** - Proxy routing (handle if present, not critical)
- **Subject** - Call subject (can ignore)
- **Priority** - Call priority (can ignore)
- **Organization** - Caller organization (can ignore)
- **User-Agent** - Client identification (can ignore)

---

## 3. IMPLEMENTATION PLAN

### Phase 1: Critical Dummy Handlers (HIGH Priority)

#### Task 1: Implement OPTIONS Response Handler
**Why:** Many SIP servers send OPTIONS as keepalive or capability query. Must respond.

**Implementation:**
```c
// Add to sip_task() message processing (around line 916)
else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
    sip_add_log_entry("received", "OPTIONS request received");
    
    // Extract headers for response
    char call_id[128] = {0};
    char via_header[256] = {0};
    char from_header[256] = {0};
    char to_header[256] = {0};
    int cseq_num = 1;
    
    // Parse headers (similar to INVITE/BYE handling)
    // ... header extraction code ...
    
    // Build 200 OK response with capabilities
    static char options_response[1024];
    snprintf(options_response, sizeof(options_response),
        "SIP/2.0 200 OK\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d OPTIONS\r\n"
        "Allow: INVITE, ACK, BYE, CANCEL, OPTIONS\r\n"
        "Accept: application/sdp\r\n"
        "Accept-Encoding: identity\r\n"
        "Accept-Language: en\r\n"
        "Supported: \r\n"
        "User-Agent: ESP32-Doorbell/1.0\r\n"
        "Content-Length: 0\r\n\r\n",
        via_header, from_header, to_header, call_id, cseq_num);
    
    // Send response
    struct sockaddr_in server_addr;
    if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        sendto(sip_socket, options_response, strlen(options_response), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
        sip_add_log_entry("sent", "200 OK response to OPTIONS");
    }
}
```

**Files:** `main/sip_client.c` (add around line 916)  
**Effort:** 2 hours  
**Testing:** Send OPTIONS from SIP client, verify 200 OK response

---

#### Task 2: Implement CANCEL Request Handler
**Why:** Caller may cancel call before answer. Must respond properly.

**Implementation:**
```c
// Add to sip_task() message processing (around line 916)
else if (strncmp(buffer, "CANCEL ", 7) == 0) {
    sip_add_log_entry("received", "CANCEL request received");
    
    // Only valid if we have an ongoing INVITE transaction
    if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
        // Extract headers
        char call_id[128] = {0};
        char via_header[256] = {0};
        char from_header[256] = {0};
        char to_header[256] = {0};
        int cseq_num = 1;
        
        // Parse headers (similar to BYE handling)
        // ... header extraction code ...
        
        // Send 200 OK to CANCEL
        static char cancel_response[768];
        snprintf(cancel_response, sizeof(cancel_response),
            "SIP/2.0 200 OK\r\n"
            "Via: %s\r\n"
            "From: %s\r\n"
            "To: %s\r\n"
            "Call-ID: %s\r\n"
            "CSeq: %d CANCEL\r\n"
            "User-Agent: ESP32-Doorbell/1.0\r\n"
            "Content-Length: 0\r\n\r\n",
            via_header, from_header, to_header, call_id, cseq_num);
        
        struct sockaddr_in server_addr;
        if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
            sendto(sip_socket, cancel_response, strlen(cancel_response), 0,
                   (struct sockaddr*)&server_addr, sizeof(server_addr));
            sip_add_log_entry("sent", "200 OK response to CANCEL");
            
            // Also send 487 Request Terminated to original INVITE
            // (reuse headers from INVITE transaction)
            // ... implementation ...
            
            // Clear call state
            current_state = SIP_STATE_REGISTERED;
            call_start_timestamp = 0;
            sip_add_log_entry("info", "Call cancelled by remote party");
        }
    } else {
        // Send 481 Call/Transaction Does Not Exist
        sip_add_log_entry("error", "CANCEL for unknown transaction");
        // ... send 481 response ...
    }
}
```

**Files:** `main/sip_client.c` (add around line 916)  
**Effort:** 3 hours  
**Testing:** Initiate call and cancel before answer, verify graceful handling

---

### Phase 2: Standard Error Responses (MEDIUM Priority)

#### Task 3: Implement Generic Method Handler for Unsupported Methods
**Why:** Servers may try other methods (INFO, UPDATE, PRACK, etc.). Respond with proper error codes.

**Implementation:**
```c
// Add at end of message processing in sip_task() (after all method handlers)
else if (strncmp(buffer, "INFO ", 5) == 0 ||
         strncmp(buffer, "UPDATE ", 7) == 0 ||
         strncmp(buffer, "PRACK ", 6) == 0 ||
         strncmp(buffer, "SUBSCRIBE ", 10) == 0 ||
         strncmp(buffer, "NOTIFY ", 7) == 0 ||
         strncmp(buffer, "MESSAGE ", 8) == 0 ||
         strncmp(buffer, "REFER ", 6) == 0) {
    
    // Extract method name for logging
    char method[32] = {0};
    const char* space = strchr(buffer, ' ');
    if (space) {
        size_t len = space - buffer;
        if (len < sizeof(method)) {
            strncpy(method, buffer, len);
            method[len] = '\0';
        }
    }
    
    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "%s method not implemented - sending 501", method);
    sip_add_log_entry("info", log_msg);
    
    // Extract headers for response
    char call_id[128] = {0};
    char via_header[256] = {0};
    char from_header[256] = {0};
    char to_header[256] = {0};
    int cseq_num = 1;
    
    // Parse headers (reusable header extraction function needed)
    // ... header extraction code ...
    
    // Send 501 Not Implemented
    static char not_impl_response[768];
    snprintf(not_impl_response, sizeof(not_impl_response),
        "SIP/2.0 501 Not Implemented\r\n"
        "Via: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %d %s\r\n"
        "Allow: INVITE, ACK, BYE, CANCEL, OPTIONS\r\n"
        "User-Agent: ESP32-Doorbell/1.0\r\n"
        "Content-Length: 0\r\n\r\n",
        via_header, from_header, to_header, call_id, cseq_num, method);
    
    struct sockaddr_in server_addr;
    if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        sendto(sip_socket, not_impl_response, strlen(not_impl_response), 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        snprintf(log_msg, sizeof(log_msg), "501 Not Implemented sent for %s", method);
        sip_add_log_entry("sent", log_msg);
    }
}
```

**Files:** `main/sip_client.c` (add after line 1278)  
**Effort:** 2 hours  
**Testing:** Send INFO/UPDATE/etc. requests, verify 501 responses

---

#### Task 4: Add Require Header Validation
**Why:** If server sends Require header with unsupported extensions, must reject with 420 Bad Extension.

**Implementation:**
```c
// Add early in INVITE/other request processing
const char* require_hdr = strstr(buffer, "Require:");
if (require_hdr) {
    // Extract required extensions
    require_hdr += 8;
    while (*require_hdr == ' ') require_hdr++;
    const char* require_end = strstr(require_hdr, "\r\n");
    
    if (require_end) {
        char required_ext[256];
        size_t len = require_end - require_hdr;
        if (len < sizeof(required_ext)) {
            strncpy(required_ext, require_hdr, len);
            required_ext[len] = '\0';
            
            char log_msg[256];
            snprintf(log_msg, sizeof(log_msg), 
                     "Server requires unsupported extension: %s", required_ext);
            sip_add_log_entry("error", log_msg);
            
            // Send 420 Bad Extension with Unsupported header
            static char bad_ext_response[1024];
            snprintf(bad_ext_response, sizeof(bad_ext_response),
                "SIP/2.0 420 Bad Extension\r\n"
                "Via: %s\r\n"
                "From: %s\r\n"
                "To: %s\r\n"
                "Call-ID: %s\r\n"
                "CSeq: %d INVITE\r\n"
                "Unsupported: %s\r\n"
                "User-Agent: ESP32-Doorbell/1.0\r\n"
                "Content-Length: 0\r\n\r\n",
                via_header, from_header, to_header, call_id, cseq_num, required_ext);
            
            // Send response and return (don't process request)
            // ... send code ...
            continue;  // Skip to next iteration
        }
    }
}
```

**Files:** `main/sip_client.c` (add in INVITE handler, around line 1025)  
**Effort:** 1 hour  
**Testing:** Send INVITE with Require: 100rel, verify 420 response

---

### Phase 3: Helper Functions (Refactoring)

#### Task 5: Create Reusable Header Extraction Function
**Why:** Header extraction code is duplicated across multiple request handlers.

**Implementation:**
```c
// Add new helper function
typedef struct {
    char call_id[128];
    char via_header[256];
    char from_header[256];
    char to_header[256];
    char contact[128];
    int cseq_num;
    char cseq_method[32];
    bool valid;
} sip_request_headers_t;

static sip_request_headers_t extract_request_headers(const char* buffer) {
    sip_request_headers_t headers = {0};
    
    // Extract Call-ID
    const char* cid_ptr = strstr(buffer, "Call-ID:");
    if (cid_ptr) {
        cid_ptr += 8;
        while (*cid_ptr == ' ') cid_ptr++;
        const char* cid_term = strstr(cid_ptr, "\r\n");
        if (cid_term) {
            size_t len = cid_term - cid_ptr;
            if (len < sizeof(headers.call_id) - 1) {
                strncpy(headers.call_id, cid_ptr, len);
                headers.call_id[len] = '\0';
            }
        }
    }
    
    // Extract Via
    const char* via_ptr = strstr(buffer, "Via:");
    if (via_ptr) {
        via_ptr += 4;
        while (*via_ptr == ' ') via_ptr++;
        const char* via_term = strstr(via_ptr, "\r\n");
        if (via_term) {
            size_t len = via_term - via_ptr;
            if (len < sizeof(headers.via_header) - 1) {
                strncpy(headers.via_header, via_ptr, len);
                headers.via_header[len] = '\0';
            }
        }
    }
    
    // Extract From
    const char* from_ptr = strstr(buffer, "From:");
    if (from_ptr) {
        from_ptr += 5;
        while (*from_ptr == ' ') from_ptr++;
        const char* from_term = strstr(from_ptr, "\r\n");
        if (from_term) {
            size_t len = from_term - from_ptr;
            if (len < sizeof(headers.from_header) - 1) {
                strncpy(headers.from_header, from_ptr, len);
                headers.from_header[len] = '\0';
            }
        }
    }
    
    // Extract To
    const char* to_ptr = strstr(buffer, "To:");
    if (to_ptr) {
        to_ptr += 3;
        while (*to_ptr == ' ') to_ptr++;
        const char* to_term = strstr(to_ptr, "\r\n");
        if (to_term) {
            size_t len = to_term - to_ptr;
            if (len < sizeof(headers.to_header) - 1) {
                strncpy(headers.to_header, to_ptr, len);
                headers.to_header[len] = '\0';
            }
        }
    }
    
    // Extract CSeq
    const char* cseq_ptr = strstr(buffer, "CSeq:");
    if (cseq_ptr) {
        cseq_ptr += 5;
        while (*cseq_ptr == ' ') cseq_ptr++;
        headers.cseq_num = atoi(cseq_ptr);
        
        // Extract method from CSeq
        const char* method_ptr = cseq_ptr;
        while (*method_ptr && *method_ptr != ' ') method_ptr++;
        if (*method_ptr == ' ') {
            method_ptr++;
            const char* method_end = strstr(method_ptr, "\r\n");
            if (method_end) {
                size_t len = method_end - method_ptr;
                if (len < sizeof(headers.cseq_method) - 1) {
                    strncpy(headers.cseq_method, method_ptr, len);
                    headers.cseq_method[len] = '\0';
                }
            }
        }
    }
    
    headers.valid = (strlen(headers.call_id) > 0 && 
                     strlen(headers.via_header) > 0 &&
                     strlen(headers.from_header) > 0 &&
                     strlen(headers.to_header) > 0);
    
    return headers;
}
```

**Files:** `main/sip_client.c` (add around line 300)  
**Effort:** 2 hours  
**Testing:** Refactor existing handlers to use this function

---

#### Task 6: Create Generic Response Builder
**Why:** Response building code is duplicated.

**Implementation:**
```c
static void send_sip_response(int code, const char* reason, 
                              const sip_request_headers_t* req_headers,
                              const char* extra_headers,
                              const char* body) {
    static char response[2048];
    int len = 0;
    
    // Status line
    len += snprintf(response + len, sizeof(response) - len,
                   "SIP/2.0 %d %s\r\n", code, reason);
    
    // Mandatory headers
    len += snprintf(response + len, sizeof(response) - len,
                   "Via: %s\r\n"
                   "From: %s\r\n"
                   "To: %s\r\n"
                   "Call-ID: %s\r\n"
                   "CSeq: %d %s\r\n",
                   req_headers->via_header,
                   req_headers->from_header,
                   req_headers->to_header,
                   req_headers->call_id,
                   req_headers->cseq_num,
                   req_headers->cseq_method);
    
    // Extra headers (if any)
    if (extra_headers && strlen(extra_headers) > 0) {
        len += snprintf(response + len, sizeof(response) - len,
                       "%s", extra_headers);
    }
    
    // User-Agent
    len += snprintf(response + len, sizeof(response) - len,
                   "User-Agent: ESP32-Doorbell/1.0\r\n");
    
    // Content
    if (body && strlen(body) > 0) {
        len += snprintf(response + len, sizeof(response) - len,
                       "Content-Length: %d\r\n\r\n%s",
                       strlen(body), body);
    } else {
        len += snprintf(response + len, sizeof(response) - len,
                       "Content-Length: 0\r\n\r\n");
    }
    
    // Send response
    struct sockaddr_in server_addr;
    if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        sendto(sip_socket, response, len, 0,
               (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "%d %s sent", code, reason);
        sip_add_log_entry("sent", log_msg);
    }
}
```

**Files:** `main/sip_client.c` (add around line 350)  
**Effort:** 2 hours  
**Testing:** Refactor all response sending to use this function

---

## 4. SUMMARY OF CHANGES NEEDED

### Files to Modify
- **main/sip_client.c** - Add all dummy handlers and helper functions

### Code Changes Summary
1. Add OPTIONS handler (~50 lines)
2. Add CANCEL (receive) handler (~80 lines)
3. Add generic unsupported method handler (~60 lines)
4. Add Require header validation (~40 lines)
5. Add header extraction helper (~100 lines)
6. Add response builder helper (~60 lines)
7. Refactor existing handlers to use helpers (~50 lines of changes)

**Total New/Modified Code:** ~440 lines

### Testing Requirements
- [ ] Test OPTIONS keepalive from server
- [ ] Test CANCEL from caller before answer
- [ ] Test INFO/UPDATE/PRACK/SUBSCRIBE/NOTIFY/MESSAGE/REFER methods → verify 501
- [ ] Test INVITE with Require: unsupported-extension → verify 420
- [ ] Test all changes with Fritz!Box
- [ ] Test with Asterisk
- [ ] Test with FreeSWITCH

---

## 5. PRIORITY IMPLEMENTATION ORDER

### Week 1 (Critical for Production)
1. ✅ Task 5: Header extraction helper (refactoring foundation)
2. ✅ Task 6: Generic response builder (refactoring foundation)
3. ⚠️ Task 1: OPTIONS handler (keepalive support)
4. ⚠️ Task 2: CANCEL handler (call cancellation support)

### Week 2 (Nice to Have)
5. Task 3: Generic unsupported method handler
6. Task 4: Require header validation
7. Refactor existing handlers to use helpers
8. Testing with multiple SIP servers

---

## 6. DOORSTATION-SPECIFIC RECOMMENDATIONS

### What NOT to Implement (Keep as 501 Not Implemented)
- **SUBSCRIBE/NOTIFY** - Not needed for doorstation (no presence required)
- **MESSAGE** - Not needed (no instant messaging)
- **REFER** - Not needed (no call transfer)
- **UPDATE** - Not needed (no session modification before ACK)
- **PRACK** - Not needed (reliable provisional responses not critical)

### What MUST Work (Already Implemented)
- ✅ REGISTER with authentication
- ✅ INVITE (receive incoming calls)
- ✅ ACK (establish calls)
- ✅ BYE (end calls)
- ✅ SDP with PCMU/PCMA codecs
- ✅ RTP audio

### What SHOULD Work (This Implementation Plan)
- ⚠️ OPTIONS (respond to capability queries)
- ⚠️ CANCEL (handle call cancellation)
- ⚠️ All unsupported methods (graceful 501 rejection)
- ⚠️ Require header (reject unsupported features with 420)

---

## 7. ESTIMATED EFFORT

| Phase | Tasks | Lines of Code | Effort (hours) | Priority |
|-------|-------|---------------|----------------|----------|
| Phase 1 | Tasks 1-2 | ~130 | 5 | HIGH |
| Phase 2 | Tasks 3-4 | ~100 | 3 | MEDIUM |
| Phase 3 | Tasks 5-6 | ~210 | 4 | HIGH (foundation) |
| **Total** | **6 tasks** | **~440** | **12 hours** | |

**Timeline:** 2 weeks (6 hours/week)

---

## 8. CONCLUSION

The current doorstation implementation **already has all essential features** for basic operation. This implementation plan adds **graceful handling of optional features** that some SIP servers may request but aren't critical for doorstation functionality.

**Key Benefits:**
- ✅ Works with more SIP servers (better interoperability)
- ✅ Better logging of unsupported features
- ✅ Cleaner code (helper functions reduce duplication)
- ✅ RFC 3261 compliance improved (proper error responses)

**Recommended Action:** Implement **Phase 1 (Tasks 1-2) and Phase 3 (Tasks 5-6)** first for best results. Phase 2 can be added later if needed.

---

**Document End**