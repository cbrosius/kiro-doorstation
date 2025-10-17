# Design Document

## Overview

This design document describes the refactoring of the web server implementation to separate API endpoint handlers into a dedicated module. The refactoring will split the monolithic web_server.c file (3134 lines) into two focused modules: web_server.c for HTML serving and server lifecycle, and web_api.c for all API endpoint handling.

The design maintains backward compatibility with all existing API endpoints while improving code organization and maintainability.

## Architecture

### Module Structure

```
main/
├── web_server.c          (~400 lines)
│   ├── Server lifecycle (start/stop)
│   ├── HTML page handlers
│   ├── Authentication filter
│   ├── Certificate loading
│   └── HTTP-to-HTTPS redirect
│
├── web_server.h          (~20 lines)
│   ├── web_server_start()
│   ├── web_server_stop()
│   └── auth_filter()
│
├── web_api.c             (~2500 lines)
│   ├── All API handlers
│   ├── Email config helpers
│   ├── JSON request/response logic
│   └── URI handler registration
│
└── web_api.h             (~10 lines)
    └── web_api_register_handlers()
```

### Interaction Flow

```
[Client Request] 
    ↓
[ESP-IDF HTTP Server]
    ↓
    ├─→ /api/* → [web_api.c handlers]
    │              ↓
    │         [auth_filter() from web_server.c]
    │              ↓
    │         [Process & respond]
    │
    └─→ /*.html → [web_server.c handlers]
                   ↓
              [auth_filter()]
                   ↓
              [Serve HTML]
```

## Components and Interfaces

### web_server.c (Retained Components)

**Responsibilities:**
- HTTPS server initialization and lifecycle management
- Certificate and key loading from NVS
- HTML page serving (index, login, setup, documentation)
- Authentication filter implementation
- HTTP-to-HTTPS redirect server
- Public endpoint checking

**Key Functions:**
```c
// Public API
void web_server_start(void);
void web_server_stop(void);
esp_err_t auth_filter(httpd_req_t *req);

// Internal functions
static bool is_public_endpoint(const char* uri);
static esp_err_t load_cert_and_key(char** cert_pem, size_t* cert_size, 
                                    char** key_pem, size_t* key_size);
static esp_err_t http_redirect_handler(httpd_req_t *req, httpd_err_code_t err);

// HTML page handlers
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t documentation_handler(httpd_req_t *req);
static esp_err_t login_handler(httpd_req_t *req);
static esp_err_t setup_handler(httpd_req_t *req);
```

**Static Variables:**
```c
static httpd_handle_t server = NULL;
static httpd_handle_t redirect_server = NULL;
```

### web_api.c (New Module)

**Responsibilities:**
- All API endpoint handler implementations
- JSON request parsing and response generation
- Email configuration management (moved from web_server.c)
- URI handler structure definitions
- Bulk registration of all API endpoints

**API Categories:**
1. **Authentication API** (5 endpoints)
   - POST /api/auth/login
   - POST /api/auth/logout
   - POST /api/auth/set-password
   - POST /api/auth/change-password
   - GET /api/auth/logs

2. **SIP API** (8 endpoints)
   - GET /api/sip/status
   - GET /api/sip/config
   - POST /api/sip/config
   - POST /api/sip/test
   - POST /api/sip/testcall
   - GET /api/sip/log
   - POST /api/sip/connect
   - POST /api/sip/disconnect

3. **WiFi API** (5 endpoints)
   - GET /api/wifi/config
   - POST /api/wifi/config
   - GET /api/wifi/status
   - POST /api/wifi/scan
   - POST /api/wifi/connect

4. **Network API** (1 endpoint)
   - GET /api/network/ip

5. **Email API** (2 endpoints)
   - GET /api/email/config
   - POST /api/email/config

6. **System API** (3 endpoints)
   - GET /api/system/status
   - GET /api/system/info
   - POST /api/system/restart

7. **NTP API** (4 endpoints)
   - GET /api/ntp/status
   - GET /api/ntp/config
   - POST /api/ntp/config
   - POST /api/ntp/sync

8. **DTMF Security API** (3 endpoints)
   - GET /api/dtmf/security
   - POST /api/dtmf/security
   - GET /api/dtmf/logs

9. **Hardware Test API** (6 endpoints)
   - POST /api/hardware/test/doorbell
   - POST /api/hardware/test/door
   - POST /api/hardware/test/light
   - GET /api/hardware/state
   - GET /api/hardware/status
   - POST /api/hardware/test/stop

10. **Certificate Management API** (5 endpoints)
    - GET /api/cert/info
    - POST /api/cert/upload
    - POST /api/cert/generate
    - GET /api/cert/download
    - DELETE /api/cert

11. **OTA API** (1 endpoint)
    - GET /api/ota/version

**Key Functions:**
```c
// Public registration function
void web_api_register_handlers(httpd_handle_t server);

// Email configuration helpers (moved from web_server.c)
static void email_save_config(const email_config_t *config);
static email_config_t email_load_config(void);

// All API handler functions (43 total)
static esp_err_t get_sip_status_handler(httpd_req_t *req);
static esp_err_t post_auth_login_handler(httpd_req_t *req);
// ... (all other handlers)
```

### web_server.h (Updated Header)

```c
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

// Server lifecycle functions
void web_server_start(void);
void web_server_stop(void);

// Authentication filter (exposed for API module)
esp_err_t auth_filter(httpd_req_t *req);

#endif
```

### web_api.h (New Header)

```c
#ifndef WEB_API_H
#define WEB_API_H

#include "esp_http_server.h"

// Register all API endpoint handlers with the server
void web_api_register_handlers(httpd_handle_t server);

#endif
```

## Data Models

### Email Configuration Structure (Moved to web_api.c)

```c
typedef struct {
    char smtp_server[64];
    uint16_t smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char recipient_email[64];
    bool enabled;
    bool configured;
} email_config_t;
```

This structure and its associated NVS functions will be moved from web_server.c to web_api.c since they are only used by the email API handlers.

## Error Handling

### Authentication Errors

Both modules will use the shared `auth_filter()` function which handles:
- Missing session cookies → 401 Unauthorized (API) or redirect to /login.html (HTML)
- Invalid/expired sessions → 401 Unauthorized (API) or redirect to /login.html (HTML)
- No password set → 403 Forbidden (API) or redirect to /setup.html (HTML)

### API Error Responses

All API handlers maintain existing error handling patterns:
- 400 Bad Request for invalid JSON or missing parameters
- 401 Unauthorized for authentication failures
- 404 Not Found for missing resources
- 408 Request Timeout for socket timeouts
- 429 Too Many Requests for rate limiting
- 500 Internal Server Error for unexpected failures

Error responses use consistent JSON format:
```json
{
    "error": "Error message description",
    "success": false
}
```

### Module Initialization Errors

The `web_api_register_handlers()` function will log registration failures but will not fail the entire server startup if individual handlers fail to register. This ensures maximum availability even if some endpoints are unavailable.

## Testing Strategy

### Unit Testing Approach

While this is an embedded C project without a formal unit testing framework, the refactoring enables better testability:

1. **Compilation Testing**
   - Verify both modules compile without errors
   - Check for no circular dependencies
   - Validate all function declarations match implementations

2. **Integration Testing**
   - Test all 43 API endpoints after refactoring
   - Verify authentication filter works for both modules
   - Confirm HTML pages still serve correctly
   - Test HTTP-to-HTTPS redirect functionality

3. **Functional Testing Categories**
   - Authentication flow (login, logout, password management)
   - SIP configuration and status endpoints
   - WiFi scanning and connection
   - Hardware test endpoints
   - Certificate management operations
   - System information and restart

### Testing Checklist

**Pre-refactoring:**
- [ ] Document current API response formats
- [ ] Test all endpoints and record expected behavior
- [ ] Verify authentication requirements for each endpoint

**Post-refactoring:**
- [ ] Compile firmware without errors
- [ ] Flash and boot device successfully
- [ ] Access web interface via HTTPS
- [ ] Test login/logout flow
- [ ] Verify each API category (11 categories)
- [ ] Confirm no regression in functionality
- [ ] Check memory usage remains acceptable

### Manual Testing Procedure

1. **Server Startup**
   - Verify HTTPS server starts on port 443
   - Verify HTTP redirect server starts on port 80
   - Check log output for successful handler registration

2. **HTML Pages**
   - Access / (index.html)
   - Access /login.html
   - Access /setup.html
   - Access /documentation.html

3. **Authentication**
   - Test initial password setup
   - Test login with correct credentials
   - Test login with incorrect credentials
   - Test logout
   - Test password change

4. **API Endpoints** (Sample from each category)
   - GET /api/system/status
   - GET /api/sip/status
   - GET /api/wifi/status
   - POST /api/wifi/scan
   - GET /api/ntp/status

## Migration Strategy

### Phase 1: Create New Module Structure
1. Create web_api.h with registration function declaration
2. Create web_api.c with necessary includes
3. Update web_server.h to expose auth_filter

### Phase 2: Move API Handlers
1. Copy all API handler functions to web_api.c
2. Copy all API URI handler structures to web_api.c
3. Move email_config_t and related functions to web_api.c
4. Implement web_api_register_handlers() function

### Phase 3: Update web_server.c
1. Remove all API handler functions
2. Remove all API URI handler structures
3. Remove email configuration code
4. Add call to web_api_register_handlers() in web_server_start()
5. Make auth_filter() non-static and add to header

### Phase 4: Verification
1. Compile and resolve any errors
2. Flash firmware to device
3. Execute testing checklist
4. Verify no functional regressions

## Dependencies

### External Dependencies
- ESP-IDF HTTP server library (esp_http_server.h)
- ESP-IDF HTTPS server library (esp_https_server.h)
- cJSON library for JSON parsing
- All existing component headers (sip_client.h, wifi_manager.h, etc.)

### Internal Dependencies
- web_api.c depends on web_server.h for auth_filter()
- web_server.c depends on web_api.h for handler registration
- Both modules depend on auth_manager.h for authentication functions

### Build System
- CMakeLists.txt in main/ directory already includes all .c files
- No changes needed to build configuration
- Both files will be compiled and linked automatically

## Performance Considerations

### Memory Impact
- No significant change in RAM usage (same functions, different file)
- Slight increase in code size due to additional function call overhead (~100 bytes)
- Stack usage remains identical

### Execution Performance
- Negligible performance impact (one additional function call during startup)
- No impact on request handling performance
- Authentication filter remains inline for all handlers

## Security Considerations

### Authentication Enforcement
- auth_filter() remains centralized and shared
- All protected endpoints continue to use the same authentication logic
- No changes to session management or cookie handling

### Code Isolation
- API handlers isolated from server lifecycle code
- Reduced risk of accidentally breaking authentication when modifying APIs
- Clearer security boundaries between modules

## Future Extensibility

### Adding New API Endpoints
With the new structure, adding API endpoints becomes simpler:

1. Add handler function to web_api.c
2. Add URI structure to web_api.c
3. Register handler in web_api_register_handlers()
4. No changes needed to web_server.c

### Potential Future Improvements
- Further split web_api.c by functional area (web_api_sip.c, web_api_wifi.c, etc.)
- Create a macro-based registration system to reduce boilerplate
- Add API versioning support (e.g., /api/v1/, /api/v2/)
- Implement OpenAPI/Swagger documentation generation
