# SIP Implementation Tasks - Doorstation

**Priority:** Implement graceful handling of optional SIP features  
**Goal:** Add dummy handlers that log "not yet implemented" for features the server may request  
**Status:** Planning Phase

---

## ✅ Already Working (No Action Needed)

The doorstation already has all **essential** features implemented:

- ✅ REGISTER with digest authentication
- ✅ INVITE (incoming & outgoing calls)
- ✅ ACK (call acknowledgment)
- ✅ BYE (call termination, both directions)
- ✅ 200 OK, 180 Ringing, 401 Unauthorized responses
- ✅ Basic SDP with PCMU/PCMA/telephone-event codecs
- ✅ RTP audio streaming

**Current Status:** Doorstation works with Fritz!Box and basic SIP servers.

---

## 🔧 Tasks to Implement (Prioritized)

### HIGH PRIORITY - Week 1 (Foundation & Critical)

#### ✅ Task 1: Create Header Extraction Helper Function [COMPLETED]
**File:** `main/sip_client.c` (line 348)
**Lines:** 130 (actual)
**Effort:** 2 hours
**Why:** Foundation for all other tasks - reduces code duplication

**Implementation:**
```c
typedef struct {
    char call_id[128];
    char via_header[256];
    char from_header[256];
    char to_header[256];
    int cseq_num;
    char cseq_method[32];
    bool valid;
} sip_request_headers_t;

static sip_request_headers_t extract_request_headers(const char* buffer);
```

**Testing:** Extract headers from sample SIP messages, verify correctness

---

#### ✅ Task 2: Create Generic Response Builder Helper [COMPLETED]
**File:** `main/sip_client.c` (line 477)
**Lines:** 85 (actual)
**Effort:** 2 hours
**Why:** Foundation for all response sending - cleaner code

**Implementation:**
```c
static void send_sip_response(int code, const char* reason,
                              const sip_request_headers_t* req_headers,
                              const char* extra_headers,
                              const char* body);
```

**Testing:** Send various responses (200, 501, 420), verify formatting

---

#### ✅ Task 3: Implement OPTIONS Handler [COMPLETED]
**File:** `main/sip_client.c` (line 1187)
**Lines:** 27 (actual)
**Effort:** 1 hour
**Why:** CRITICAL - Many SIP servers send OPTIONS for keepalive/capability query

**Implementation:**
```c
else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
    sip_add_log_entry("received", "OPTIONS request");
    sip_request_headers_t headers = extract_request_headers(buffer);
    
    char allow_header[] = "Allow: INVITE, ACK, BYE, CANCEL, OPTIONS\r\n"
                         "Accept: application/sdp\r\n"
                         "Supported: \r\n";
    
    send_sip_response(200, "OK", &headers, allow_header, NULL);
}
```

**Testing:** 
- Send OPTIONS from SIP client → verify 200 OK response with Allow header
- Check Fritz!Box logs for OPTIONS keepalive → verify response

---

#### ✅ Task 4: Implement CANCEL (Receive) Handler [COMPLETED]
**File:** `main/sip_client.c` (line 1212)
**Lines:** 41 (actual)
**Effort:** 2 hours
**Why:** CRITICAL - Caller must be able to cancel calls before answer

**Implementation:**
```c
else if (strncmp(buffer, "CANCEL ", 7) == 0) {
    sip_add_log_entry("received", "CANCEL request");
    
    if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
        sip_request_headers_t headers = extract_request_headers(buffer);
        
        // Send 200 OK to CANCEL
        send_sip_response(200, "OK", &headers, NULL, NULL);
        
        // Send 487 Request Terminated to original INVITE
        // (using stored INVITE transaction headers)
        
        // Clear call state
        current_state = SIP_STATE_REGISTERED;
        call_start_timestamp = 0;
        sip_add_log_entry("info", "Call cancelled by remote party");
    } else {
        // Send 481 Call/Transaction Does Not Exist
        sip_request_headers_t headers = extract_request_headers(buffer);
        send_sip_response(481, "Call/Transaction Does Not Exist", &headers, NULL, NULL);
    }
}
```

**Testing:**
- Initiate call from SIP phone → cancel before doorstation answers → verify graceful handling
- Check logs show "Call cancelled by remote party"
- Verify 200 OK sent to CANCEL
- Verify 487 sent to INVITE

---

### MEDIUM PRIORITY - Week 2 (Enhanced Error Handling)

#### ✅ Task 5: Implement Generic Unsupported Method Handler [COMPLETED]
**File:** `main/sip_client.c` (line 1347)
**Lines:** 46 (actual)
**Effort:** 1 hour
**Why:** Gracefully reject INFO, UPDATE, PRACK, SUBSCRIBE, NOTIFY, MESSAGE, REFER

**Implementation:**
```c
else if (strncmp(buffer, "INFO ", 5) == 0 ||
         strncmp(buffer, "UPDATE ", 7) == 0 ||
         strncmp(buffer, "PRACK ", 6) == 0 ||
         strncmp(buffer, "SUBSCRIBE ", 10) == 0 ||
         strncmp(buffer, "NOTIFY ", 7) == 0 ||
         strncmp(buffer, "MESSAGE ", 8) == 0 ||
         strncmp(buffer, "REFER ", 6) == 0) {
    
    // Extract method name
    char method[32] = {0};
    const char* space = strchr(buffer, ' ');
    if (space) {
        size_t len = space - buffer;
        strncpy(method, buffer, len < sizeof(method) ? len : sizeof(method)-1);
    }
    
    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "%s method not implemented", method);
    sip_add_log_entry("info", log_msg);
    
    sip_request_headers_t headers = extract_request_headers(buffer);
    char allow_header[] = "Allow: INVITE, ACK, BYE, CANCEL, OPTIONS\r\n";
    send_sip_response(501, "Not Implemented", &headers, allow_header, NULL);
}
```

**Testing:**
- Send INFO request → verify 501 Not Implemented with Allow header
- Send UPDATE, PRACK, SUBSCRIBE → verify 501 for each
- Check logs show "[method] not implemented"

---

#### ✅ Task 6: Add Require Header Validation [COMPLETED]
**File:** `main/sip_client.c` (line 1167)
**Lines:** 43 (actual)
**Effort:** 1 hour
**Why:** Properly reject requests requiring unsupported extensions

**Implementation:**
```c
// Add early in INVITE request processing, before accepting call
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
                     "Rejecting INVITE - unsupported extension required: %s", required_ext);
            sip_add_log_entry("error", log_msg);
            
            sip_request_headers_t headers = extract_request_headers(buffer);
            char unsupported_hdr[512];
            snprintf(unsupported_hdr, sizeof(unsupported_hdr),
                     "Unsupported: %s\r\n", required_ext);
            send_sip_response(420, "Bad Extension", &headers, unsupported_hdr, NULL);
            continue; // Don't process this INVITE
        }
    }
}
```

**Testing:**
- Send INVITE with `Require: 100rel` → verify 420 Bad Extension with Unsupported header
- Send INVITE with `Require: replaces` → verify 420 response
- Check logs show "unsupported extension required"

---

### OPTIONAL - Week 3 (Code Quality)

#### ☐ Task 7: Refactor Existing Handlers to Use Helper Functions
**File:** `main/sip_client.c` (modify existing INVITE, BYE, OPTIONS handlers)  
**Lines:** ~50 changes  
**Effort:** 2 hours  
**Why:** Reduce code duplication, improve maintainability

**Changes:**
- Replace header extraction in INVITE handler with `extract_request_headers()`
- Replace header extraction in BYE handler with `extract_request_headers()`
- Replace response building in multiple places with `send_sip_response()`

**Testing:**
- Regression test all existing functionality
- Verify INVITE, BYE, OPTIONS still work correctly
- Check code is cleaner and more maintainable

---

## 📊 Implementation Summary

| Task | Priority | Effort | Lines | Status |
|------|----------|--------|-------|--------|
| 1. Header extraction helper | HIGH | 2h | 130 | ✅ Done |
| 2. Response builder helper | HIGH | 2h | 85 | ✅ Done |
| 3. OPTIONS handler | HIGH | 1h | 27 | ✅ Done |
| 4. CANCEL handler | HIGH | 2h | 41 | ✅ Done |
| 5. Unsupported method handler | MEDIUM | 1h | 46 | ✅ Done |
| 6. Require header validation | MEDIUM | 1h | 43 | ✅ Done |
| 7. Refactor with helpers | OPTIONAL | 2h | 50 | ☐ Todo |
| **TOTAL** | | **11h** | **~440** | **6/7 Done** |

---

## 🧪 Testing Checklist

### After Tasks 1-4 (Week 1)
- [ ] Send OPTIONS from SIP client → verify 200 OK with Allow header
- [ ] Fritz!Box sends OPTIONS keepalive → verify doorstation responds
- [ ] Call doorstation, cancel before answer → verify graceful handling
- [ ] Test all existing functionality still works (REGISTER, INVITE, BYE)

### After Tasks 5-6 (Week 2)
- [ ] Send INFO request → verify 501 Not Implemented
- [ ] Send UPDATE, PRACK, SUBSCRIBE requests → verify 501 for each
- [ ] Send INVITE with Require: 100rel → verify 420 Bad Extension
- [ ] Check all logs show appropriate "not implemented" messages

### After Task 7 (Week 3)
- [ ] Regression test all functionality
- [ ] Review code for readability and maintainability
- [ ] Test with Fritz!Box
- [ ] Test with Asterisk (if available)

---

## 📝 Notes

### Why These Tasks?
The current implementation **already works** for basic doorstation operation. These tasks add:

1. **Better interoperability** - More SIP servers will work correctly
2. **Better logging** - Clear indication when unsupported features are requested
3. **Cleaner code** - Helper functions reduce duplication
4. **RFC compliance** - Proper error responses per RFC 3261

### What We're NOT Implementing
- Transaction state machine (complex, not needed for basic operation)
- RFC 3261 timers A-K (complex, UDP retransmission works well enough)
- TCP transport (UDP sufficient for doorstation)
- Full SDP negotiation (basic SDP works with all tested servers)
- SUBSCRIBE/NOTIFY, MESSAGE, REFER (not needed for doorstation)

### Implementation Order Rationale
1. **Tasks 1-2 first** - Foundation functions needed by all other tasks
2. **Tasks 3-4 next** - Critical for production use (OPTIONS keepalive, CANCEL support)
3. **Tasks 5-6 later** - Nice to have, improves error handling
4. **Task 7 optional** - Code quality improvement, not functionally required

---

## 🎯 Success Criteria

After implementing tasks 1-4:
- ✅ Doorstation responds to OPTIONS keepalive from Fritz!Box
- ✅ Calls can be cancelled before answer without errors
- ✅ Logs clearly show when unsupported features are requested
- ✅ All existing functionality (REGISTER, INVITE, BYE) still works

After implementing tasks 5-6:
- ✅ All unsupported SIP methods get proper 501 response
- ✅ Requests with unsupported Require header get 420 response
- ✅ Better RFC 3261 compliance

After implementing task 7:
- ✅ Code is cleaner and more maintainable
- ✅ Less duplication in header extraction and response building

---

## 📚 Related Documents

- [`docs/RFC3261_COMPLIANCE_ANALYSIS.md`](RFC3261_COMPLIANCE_ANALYSIS.md) - Full RFC compliance analysis
- [`docs/DOORSTATION_SIP_REQUIREMENTS.md`](DOORSTATION_SIP_REQUIREMENTS.md) - Detailed requirements and implementation examples
- [`main/sip_client.c`](../main/sip_client.c) - Current SIP client implementation
- [`main/sip_client.h`](../main/sip_client.h) - SIP client header file

---

**Last Updated:** 2025-10-26  
**Status:** Ready for implementation