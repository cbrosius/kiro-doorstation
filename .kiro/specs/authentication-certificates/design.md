# Authentication & Certificates - Design Document

## Overview

This feature implements comprehensive web interface security through username/password authentication and TLS/SSL certificate management. It protects the ESP32 SIP Door Station from unauthorized access and enables encrypted HTTPS communication for production deployments.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Web Browser    │
│  (HTTPS Client) │
└────────┬────────┘
         │ HTTPS (TLS 1.2+)
         │ Session Cookie
         ▼
┌─────────────────┐
│  HTTPS Server   │
│  (ESP32)        │
│  - TLS Handler  │
│  - Auth Filter  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Auth Manager    │
│  - Login        │
│  - Session Mgmt │
│  - Password     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Certificate Mgr │
│  - TLS Cert     │
│  - Private Key  │
│  - Validation   │
└─────────────────┘
```

### Authentication Flow

```
Client Request → TLS Handshake → Session Check → Auth Filter → Handler
                      ↓               ↓              ↓
                  Certificate    Valid Session?   Protected?
                  Validation         ↓              ↓
                                 Yes → Allow    Yes → Require Auth
                                 No → Login     No → Allow
```

## Components and Interfaces

### 1. Authentication Manager Module (`auth_manager.c/h`)

**Purpose**: Manages user authentication, sessions, and password security.

**Public API**:
```c
// Initialize authentication system
void auth_manager_init(void);

// Authenticate user
typedef struct {
    bool authenticated;
    char session_id[33];  // 32 hex chars + null
    uint32_t expires_at;
    char error_message[128];
} auth_result_t;

auth_result_t auth_login(const char* username, const char* password, const char* client_ip);

// Validate session
bool auth_validate_session(const char* session_id);

// Logout
void auth_logout(const char* session_id);

// Change password
esp_err_t auth_change_password(const char* current_password, const char* new_password);

// Set initial password (first boot)
esp_err_t auth_set_initial_password(const char* password);

// Check if password is set
bool auth_is_password_set(void);

// Session management
void auth_cleanup_expired_sessions(void);
void auth_extend_session(const char* session_id);

// Login attempt tracking
typedef struct {
    char ip_address[16];
    uint32_t failed_attempts;
    uint32_t last_attempt_time;
    bool blocked;
    uint32_t block_until;
} login_attempts_t;

bool auth_is_ip_blocked(const char* ip_address);
void auth_record_failed_attempt(const char* ip_address);
void auth_clear_failed_attempts(const char* ip_address);
```

**Password Hashing**:
```c
// Use mbedtls PBKDF2 for password hashing
#define AUTH_SALT_SIZE 16
#define AUTH_HASH_SIZE 32
#define AUTH_ITERATIONS 10000

typedef struct {
    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t hash[AUTH_HASH_SIZE];
} password_hash_t;

esp_err_t auth_hash_password(const char* password, password_hash_t* output);
bool auth_verify_password(const char* password, const password_hash_t* stored_hash);
```

### 2. Certificate Manager Module (`cert_manager.c/h`)

**Purpose**: Manages TLS certificates, private keys, and certificate operations.

**Public API**:
```c
// Initialize certificate manager
void cert_manager_init(void);

// Get current certificate info
typedef struct {
    bool is_self_signed;
    char common_name[64];
    char issuer[128];
    char not_before[32];
    char not_after[32];
    uint32_t days_until_expiry;
    bool is_expired;
    bool is_expiring_soon;  // < 30 days
} cert_info_t;

esp_err_t cert_get_info(cert_info_t* info);

// Generate self-signed certificate
esp_err_t cert_generate_self_signed(const char* common_name, uint32_t validity_days);

// Upload custom certificate
esp_err_t cert_upload_custom(const char* cert_pem, const char* key_pem, const char* chain_pem);

// Validate certificate and key
esp_err_t cert_validate(const char* cert_pem, const char* key_pem);

// Get certificate PEM (for download)
esp_err_t cert_get_pem(char** cert_pem, size_t* cert_size);

// Delete certificate
esp_err_t cert_delete(void);

// Check if certificate exists
bool cert_exists(void);
```

### 3. HTTPS Server Integration

**Modified Web Server**:
```c
// web_server.c modifications

// Start HTTPS server instead of HTTP
httpd_handle_t start_https_server(void) {
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    
    // Load certificate and key
    extern const unsigned char servercert_start[] asm("_binary_servercert_pem_start");
    extern const unsigned char serverkey_start[] asm("_binary_serverkey_pem_start");
    
    conf.servercert = servercert_start;
    conf.servercert_len = servercert_end - servercert_start;
    conf.prvtkey_pem = serverkey_start;
    conf.prvtkey_len = serverkey_end - serverkey_start;
    
    httpd_handle_t server = NULL;
    if (httpd_ssl_start(&server, &conf) == ESP_OK) {
        // Register URI handlers with auth filter
        register_uri_handlers(server);
    }
    return server;
}
```

### 4. Authentication Filter

**HTTP Request Filter**:
```c
// Authentication filter for protected endpoints
static esp_err_t auth_filter(httpd_req_t *req) {
    // Skip auth for login page and public assets
    if (is_public_endpoint(req->uri)) {
        return ESP_OK;
    }
    
    // Check session cookie
    char session_id[33] = {0};
    if (httpd_req_get_cookie_val(req, "session_id", session_id, sizeof(session_id)) != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Authentication required", -1);
        return ESP_FAIL;
    }
    
    // Validate session
    if (!auth_validate_session(session_id)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Session expired", -1);
        return ESP_FAIL;
    }
    
    // Extend session on activity
    auth_extend_session(session_id);
    
    return ESP_OK;
}

// Register filter for all handlers
httpd_register_uri_handler(server, &uri);
httpd_register_err_handler(server, HTTPD_401_UNAUTHORIZED, auth_filter);
```

## Data Models

### Session Structure
```c
typedef struct {
    char session_id[33];
    char username[32];
    char ip_address[16];
    uint32_t created_at;
    uint32_t last_activity;
    uint32_t expires_at;
    bool valid;
} session_t;

#define MAX_SESSIONS 5
static session_t active_sessions[MAX_SESSIONS];
```

### User Credentials
```c
typedef struct {
    char username[32];
    password_hash_t password_hash;
    char role[16];  // "admin", "operator", "viewer"
    bool enabled;
    uint32_t created_at;
    uint32_t last_login;
} user_credentials_t;
```

### Certificate Storage
```c
typedef struct {
    char cert_pem[4096];
    char key_pem[2048];
    char chain_pem[4096];  // Optional intermediate certs
    bool is_self_signed;
    uint32_t generated_at;
} certificate_storage_t;
```

## Error Handling

### Authentication Errors
```c
#define AUTH_ERR_INVALID_CREDENTIALS  "Invalid username or password"
#define AUTH_ERR_ACCOUNT_LOCKED       "Account temporarily locked due to failed attempts"
#define AUTH_ERR_SESSION_EXPIRED      "Session has expired"
#define AUTH_ERR_WEAK_PASSWORD        "Password does not meet security requirements"
#define AUTH_ERR_PASSWORD_MISMATCH    "Current password is incorrect"
```

### Certificate Errors
```c
#define CERT_ERR_INVALID_FORMAT       "Invalid certificate format"
#define CERT_ERR_KEY_MISMATCH         "Private key does not match certificate"
#define CERT_ERR_EXPIRED              "Certificate has expired"
#define CERT_ERR_GENERATION_FAILED    "Failed to generate certificate"
#define CERT_ERR_STORAGE_FAILED       "Failed to store certificate"
```

## Testing Strategy

### Unit Tests
1. **Password Hashing Tests**
   - Test PBKDF2 hashing
   - Verify salt randomness
   - Test password verification

2. **Session Management Tests**
   - Test session creation
   - Test session validation
   - Test session expiration
   - Test session cleanup

3. **Certificate Tests**
   - Test self-signed generation
   - Test certificate validation
   - Test key pair matching
   - Test expiration detection

### Integration Tests
1. **Authentication Flow**
   - Test login with valid credentials
   - Test login with invalid credentials
   - Test session persistence
   - Test logout

2. **HTTPS Connection**
   - Test TLS handshake
   - Test certificate validation
   - Test encrypted communication
   - Test HTTP to HTTPS redirect

3. **Rate Limiting**
   - Test failed login attempts
   - Test IP blocking
   - Test block expiration

## Security Considerations

### Password Security
- **Hashing**: PBKDF2 with 10,000 iterations
- **Salt**: 16-byte random salt per password
- **Storage**: Never store plaintext passwords
- **Minimum Requirements**: 8 chars, mixed case, digit

### Session Security
- **Session ID**: 32-byte cryptographically random hex string
- **Cookie Flags**: HttpOnly, Secure, SameSite=Strict
- **Timeout**: 30 minutes of inactivity
- **Max Sessions**: 5 concurrent sessions per user

### TLS Security
- **Protocol**: TLS 1.2 minimum (TLS 1.3 if supported)
- **Cipher Suites**: Strong ciphers only (AES-GCM, ChaCha20)
- **Key Size**: 2048-bit RSA minimum
- **Certificate Validation**: Full chain validation for custom certs

### Rate Limiting
- **Failed Attempts**: 5 attempts per 15 minutes
- **Block Duration**: 5 minutes
- **IP Tracking**: Track by client IP address

## Performance Considerations

### Memory Usage
- Session storage: ~200 bytes per session × 5 = 1KB
- Certificate storage: ~10KB (cert + key + chain)
- Password hash: ~50 bytes per user
- Total additional heap: ~15KB

### CPU Usage
- Password hashing: ~100ms per login (PBKDF2)
- TLS handshake: ~200ms per connection
- Session validation: <1ms

### Flash Wear
- Password changes: Minimal (infrequent)
- Certificate updates: Minimal (rare)
- Session storage: RAM only (not persisted)

## Implementation Notes

### HTTPS Server Setup
```c
void web_server_start(void) {
    // Initialize certificate manager
    cert_manager_init();
    
    // Generate self-signed cert if none exists
    if (!cert_exists()) {
        cert_generate_self_signed("doorstation.local", 3650);
    }
    
    // Start HTTPS server
    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.port_secure = 443;
    conf.port_insecure = 80;  // For redirect
    
    // Load certificate
    cert_info_t info;
    cert_get_info(&info);
    
    // ... configure and start server
}
```

### Login Endpoint
```c
static esp_err_t post_login_handler(httpd_req_t *req) {
    char buf[256];
    httpd_req_recv(req, buf, sizeof(buf));
    
    cJSON *root = cJSON_Parse(buf);
    const char* username = cJSON_GetObjectItem(root, "username")->valuestring;
    const char* password = cJSON_GetObjectItem(root, "password")->valuestring;
    
    // Get client IP
    char client_ip[16];
    httpd_req_get_hdr_value_str(req, "X-Forwarded-For", client_ip, sizeof(client_ip));
    
    // Check if IP is blocked
    if (auth_is_ip_blocked(client_ip)) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_send(req, "{\"error\":\"Too many failed attempts\"}", -1);
        return ESP_OK;
    }
    
    // Authenticate
    auth_result_t result = auth_login(username, password, client_ip);
    
    if (result.authenticated) {
        // Set session cookie
        char cookie[128];
        snprintf(cookie, sizeof(cookie), 
                 "session_id=%s; HttpOnly; Secure; SameSite=Strict; Max-Age=1800",
                 result.session_id);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        
        httpd_resp_send(req, "{\"success\":true}", -1);
    } else {
        auth_record_failed_attempt(client_ip);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Invalid credentials\"}", -1);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}
```

### Certificate Upload Endpoint
```c
static esp_err_t post_cert_upload_handler(httpd_req_t *req) {
    // Verify admin session
    if (!auth_filter(req)) {
        return ESP_FAIL;
    }
    
    // Receive certificate data
    char cert_pem[4096];
    char key_pem[2048];
    
    // Parse multipart form data
    // ... (implementation details)
    
    // Validate certificate
    esp_err_t err = cert_validate(cert_pem, key_pem);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid certificate");
        return ESP_FAIL;
    }
    
    // Upload certificate
    err = cert_upload_custom(cert_pem, key_pem, NULL);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }
    
    // Restart HTTPS server with new certificate
    web_server_restart();
    
    httpd_resp_send(req, "{\"success\":true}", -1);
    return ESP_OK;
}
```

## Web Interface Design

### Login Page
```html
<!DOCTYPE html>
<html>
<head>
    <title>Login - ESP32 Door Station</title>
    <style>
        .login-container {
            max-width: 400px;
            margin: 100px auto;
            padding: 40px;
            background: white;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .login-form input {
            width: 100%;
            padding: 12px;
            margin: 10px 0;
            border: 1px solid #ddd;
            border-radius: 4px;
        }
        .login-btn {
            width: 100%;
            padding: 12px;
            background: #007bff;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
        }
        .error-message {
            color: #dc3545;
            margin: 10px 0;
        }
    </style>
</head>
<body>
    <div class="login-container">
        <h2>ESP32 Door Station</h2>
        <form class="login-form" onsubmit="handleLogin(event)">
            <input type="text" id="username" placeholder="Username" required>
            <input type="password" id="password" placeholder="Password" required>
            <button type="submit" class="login-btn">Login</button>
            <div id="error-message" class="error-message"></div>
        </form>
    </div>
    
    <script>
        async function handleLogin(event) {
            event.preventDefault();
            
            const username = document.getElementById('username').value;
            const password = document.getElementById('password').value;
            
            try {
                const response = await fetch('/api/auth/login', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({username, password})
                });
                
                if (response.ok) {
                    window.location.href = '/';
                } else {
                    const error = await response.json();
                    document.getElementById('error-message').textContent = error.error;
                }
            } catch (error) {
                document.getElementById('error-message').textContent = 'Login failed';
            }
        }
    </script>
</body>
</html>
```

### Certificate Management Section
```html
<div class="cert-management">
    <h3>TLS Certificate</h3>
    
    <div class="cert-info">
        <p><strong>Type:</strong> <span id="cert-type">Self-Signed</span></p>
        <p><strong>Common Name:</strong> <span id="cert-cn">doorstation.local</span></p>
        <p><strong>Expires:</strong> <span id="cert-expiry">2035-01-01</span></p>
        <p><strong>Days Remaining:</strong> <span id="cert-days">3650</span></p>
    </div>
    
    <div class="cert-actions">
        <h4>Upload Custom Certificate</h4>
        <input type="file" id="cert-file" accept=".pem,.crt">
        <input type="file" id="key-file" accept=".pem,.key">
        <button onclick="uploadCertificate()">Upload Certificate</button>
    </div>
    
    <div class="cert-actions">
        <h4>Generate New Self-Signed Certificate</h4>
        <input type="text" id="cert-common-name" placeholder="Common Name" value="doorstation.local">
        <button onclick="generateCertificate()">Generate Certificate</button>
    </div>
</div>
```

## Dependencies

### ESP-IDF Components
- `esp_https_server`: HTTPS server support
- `mbedtls`: TLS/SSL and cryptography
- `nvs_flash`: Certificate and password storage

### Project Components
- `web_server`: HTTP/HTTPS server
- `wifi_manager`: Network connectivity

## Rollout Plan

### Phase 1: Basic Authentication (MVP)
- Implement password hashing and storage
- Add login page and session management
- Protect web interface with authentication
- Add logout functionality

### Phase 2: HTTPS Support
- Generate self-signed certificates
- Enable HTTPS server
- Add HTTP to HTTPS redirect
- Display certificate information

### Phase 3: Certificate Management
- Add custom certificate upload
- Implement certificate validation
- Add certificate expiration warnings
- Support certificate chains

### Phase 4: Advanced Features
- Multi-user support with roles
- Login audit logging
- API token authentication
- Certificate auto-renewal

## References

- [ESP-IDF HTTPS Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_https_server.html)
- [mbedTLS Documentation](https://tls.mbed.org/api/)
- [OWASP Authentication Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Authentication_Cheat_Sheet.html)
- [TLS Best Practices](https://wiki.mozilla.org/Security/Server_Side_TLS)
