# Implementation Plan

- [x] 1. Create authentication manager module





  - Create `main/auth_manager.h` and `main/auth_manager.c` files
  - Define session structure and user credentials structure
  - Implement `auth_manager_init()` function
  - Define password hashing functions using mbedtls PBKDF2
  - _Requirements: 1.1, 1.2, 1.3_

- [x] 2. Implement password hashing and verification





  - Implement `auth_hash_password()` using PBKDF2 with 10,000 iterations
  - Generate random 16-byte salt for each password
  - Implement `auth_verify_password()` to compare hashed passwords
  - Add password strength validation (min 8 chars, mixed case, digit)
  - Store password hash in NVS
  - _Requirements: 3.2, 3.3, 3.4_

- [x] 3. Implement login functionality





  - Implement `auth_login()` function with username/password validation
  - Generate cryptographically random 32-byte session ID
  - Create session with 30-minute timeout
  - Return session ID and expiration time
  - Handle invalid credentials with error messages
  - _Requirements: 1.3, 1.4, 2.1_

- [x] 4. Implement session management





  - Implement `auth_validate_session()` to check session validity
  - Implement `auth_extend_session()` to reset timeout on activity
  - Implement `auth_logout()` to invalidate session
  - Implement `auth_cleanup_expired_sessions()` background task
  - Store active sessions in RAM (max 5 concurrent sessions)
  - _Requirements: 2.2, 2.3, 2.4, 2.5_

- [x] 5. Implement login rate limiting





  - Track failed login attempts by IP address
  - Implement `auth_is_ip_blocked()` to check if IP is blocked
  - Implement `auth_record_failed_attempt()` to increment counter
  - Block IP for 5 minutes after 5 failed attempts within 15 minutes
  - Clear failed attempts on successful login
  - _Requirements: 1.6_

- [x] 6. Create certificate manager module





  - Create `main/cert_manager.h` and `main/cert_manager.c` files
  - Define certificate info structure
  - Implement `cert_manager_init()` function
  - Define certificate storage structure
  - _Requirements: 5.1, 5.5_

- [x] 7. Implement self-signed certificate generation





  - Implement `cert_generate_self_signed()` using mbedtls
  - Generate 2048-bit RSA key pair
  - Create X.509 certificate with 10-year validity
  - Set Common Name to device hostname
  - Store certificate and private key in NVS
  - _Requirements: 5.2, 5.3, 5.4, 5.5_

- [x] 8. Implement certificate information retrieval





  - Implement `cert_get_info()` to parse certificate details
  - Extract Common Name, issuer, validity dates
  - Calculate days until expiration
  - Detect if certificate is self-signed
  - Detect if certificate is expiring soon (< 30 days)
  - _Requirements: 7.2, 7.3, 7.4, 7.5_

- [x] 9. Implement custom certificate upload






  - Implement `cert_validate()` to verify certificate format (PEM)
  - Verify private key matches certificate public key
  - Implement `cert_upload_custom()` to store certificate and key
  - Support certificate chains (intermediate CAs)
  - Validate certificate chain integrity
  - _Requirements: 6.2, 6.3, 6.4, 14.1, 14.2, 14.3_

- [ ] 10. Convert web server to HTTPS
  - Modify `web_server_start()` to use `httpd_ssl_start()`
  - Load certificate and private key from NVS
  - Configure HTTPS server on port 443
  - Set TLS 1.2 as minimum protocol version
  - Configure strong cipher suites only
  - _Requirements: 4.1, 4.2, 4.3, 4.4_

- [ ] 11. Implement HTTP to HTTPS redirect
  - Start HTTP server on port 80
  - Add redirect handler for all HTTP requests
  - Send 301 Moved Permanently to HTTPS URL
  - Close HTTP connection after redirect
  - _Requirements: 4.5_

- [ ] 12. Implement authentication filter for web endpoints
  - Create `auth_filter()` function to check session cookie
  - Register filter for all protected endpoints
  - Skip authentication for login page and public assets
  - Return 401 Unauthorized if session is missing or invalid
  - Extend session timeout on each authenticated request
  - _Requirements: 8.1, 8.2, 8.3, 8.4_

- [ ] 13. Create login API endpoint
  - Add `POST /api/auth/login` endpoint
  - Parse username and password from JSON body
  - Check if client IP is blocked due to rate limiting
  - Call `auth_login()` to authenticate user
  - Set session cookie with HttpOnly, Secure, SameSite=Strict flags
  - Return success or error response
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 10.1_

- [ ] 14. Create logout API endpoint
  - Add `POST /api/auth/logout` endpoint
  - Extract session ID from cookie
  - Call `auth_logout()` to invalidate session
  - Clear session cookie
  - Return success response
  - _Requirements: 2.5_

- [ ] 15. Create password management API endpoints
  - Add `POST /api/auth/set-password` endpoint for initial setup
  - Add `POST /api/auth/change-password` endpoint
  - Validate current password before allowing change
  - Enforce password strength requirements
  - Invalidate all sessions after password change
  - _Requirements: 3.1, 3.2, 3.3, 3.5, 3.6_

- [ ] 16. Create certificate management API endpoints
  - Add `GET /api/cert/info` endpoint to retrieve certificate details
  - Add `POST /api/cert/upload` endpoint for custom certificate upload
  - Add `POST /api/cert/generate` endpoint for self-signed generation
  - Add `GET /api/cert/download` endpoint to download current certificate
  - Add `DELETE /api/cert` endpoint to delete certificate
  - _Requirements: 6.1, 6.2, 6.5, 7.1_

- [ ] 17. Create login web page
  - Create `login.html` with username and password fields
  - Add form submission handler to call `/api/auth/login`
  - Display error messages for failed login attempts
  - Redirect to main interface on successful login
  - Add styling for professional appearance
  - _Requirements: 1.1, 1.2_

- [ ] 18. Add certificate management section to web interface
  - Add "Security" menu item to sidebar navigation
  - Display current certificate information (type, CN, expiration)
  - Add file upload interface for custom certificate and private key
  - Add button to generate new self-signed certificate
  - Display warning when certificate is expiring soon
  - Add download button for current certificate
  - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5_

- [ ] 19. Implement session check on page load
  - Add JavaScript to check session validity on page load
  - Redirect to login page if session is invalid or missing
  - Add session timeout warning (5 minutes before expiration)
  - Implement automatic session extension on user activity
  - _Requirements: 2.3, 2.4_

- [ ] 20. Add login audit logging
  - Implement login attempt logging with timestamp, username, IP, and result
  - Store last 100 login attempts in circular buffer
  - Add `GET /api/auth/logs` endpoint to retrieve login logs
  - Display login logs in web interface security section
  - Log successful logins, failed attempts, and blocked IPs
  - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

- [ ] 21. Implement initial setup flow
  - Check if admin password is set on first boot
  - Display setup wizard if password not set
  - Require password creation before accessing main interface
  - Generate self-signed certificate during setup
  - Save configuration and redirect to login page
  - _Requirements: 3.1, 5.1_

- [ ] 22. Add certificate expiration warnings
  - Check certificate expiration on dashboard load
  - Display warning banner if certificate expires within 30 days
  - Display critical warning if certificate expires within 7 days
  - Display error if certificate has expired
  - Offer option to generate new self-signed certificate
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

- [ ] 23. Update CMakeLists.txt to include authentication modules
  - Add `auth_manager.c` to `main/CMakeLists.txt` SRCS list
  - Add `cert_manager.c` to `main/CMakeLists.txt` SRCS list
  - Verify mbedtls component is linked
  - Verify esp_https_server component is linked
  - _Requirements: 1.1, 6.1_

- [ ]* 24. Implement password reset mechanism
  - Add physical reset button GPIO handler
  - Reset admin password to default when button held for 10 seconds
  - Log password reset event
  - Require password change on next login after reset
  - Invalidate all sessions on password reset
  - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5_

- [ ]* 25. Add comprehensive security logging
  - Log all authentication events (login, logout, password change)
  - Log certificate operations (upload, generate, delete)
  - Log rate limiting events (IP blocks, unblocks)
  - Add timestamps and IP addresses to all log entries
  - Implement log rotation to prevent memory overflow
  - _Requirements: 8.5, 9.1, 9.2, 9.3_

- [ ]* 26. Create integration tests for authentication
  - Test login with valid credentials
  - Test login with invalid credentials
  - Test session validation and expiration
  - Test rate limiting and IP blocking
  - Test password change functionality
  - Test logout functionality
  - _Requirements: 1.3, 1.4, 1.6, 2.3, 2.4, 2.5, 3.5_

- [ ]* 27. Create integration tests for certificates
  - Test self-signed certificate generation
  - Test custom certificate upload
  - Test certificate validation
  - Test HTTPS connection establishment
  - Test HTTP to HTTPS redirect
  - Test certificate expiration detection
  - _Requirements: 4.3, 4.4, 4.5, 5.2, 6.2, 7.5_
