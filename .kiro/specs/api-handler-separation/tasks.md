# Implementation Plan

- [x] 1. Create new API module files and update headers





  - Create main/web_api.h with registration function declaration
  - Create main/web_api.c with initial structure and includes
  - Update main/web_server.h to expose auth_filter function
  - _Requirements: 1.4, 6.1, 6.2, 6.3_

- [x] 2. Move email configuration code to API module





  - [x] 2.1 Copy email_config_t structure definition to web_api.c


    - Move typedef struct email_config_t from web_server.c to web_api.c
    - _Requirements: 3.1_
  

  - [x] 2.2 Copy email NVS helper functions to web_api.c

    - Move email_save_config() function to web_api.c
    - Move email_load_config() function to web_api.c
    - _Requirements: 3.1_

- [x] 3. Move all API handler functions to web_api.c






  - [x] 3.1 Copy SIP API handlers (8 handlers)

    - Move get_sip_status_handler, get_sip_config_handler, post_sip_config_handler
    - Move post_sip_test_handler, post_sip_test_call_handler, get_sip_log_handler
    - Move post_sip_connect_handler, post_sip_disconnect_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.2 Copy WiFi API handlers (5 handlers)

    - Move get_wifi_config_handler, post_wifi_config_handler, get_wifi_status_handler
    - Move post_wifi_scan_handler, post_wifi_connect_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.3 Copy Network API handlers (1 handler)

    - Move get_network_ip_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.4 Copy Email API handlers (2 handlers)

    - Move get_email_config_handler, post_email_config_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.5 Copy OTA API handlers (1 handler)

    - Move get_ota_version_handler
    - _Requirements: 1.2, 5.1_
  
  - [x] 3.6 Copy System API handlers (3 handlers)


    - Move get_system_status_handler, post_system_restart_handler, get_system_info_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.7 Copy NTP API handlers (4 handlers)

    - Move get_ntp_status_handler, get_ntp_config_handler
    - Move post_ntp_config_handler, post_ntp_sync_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.8 Copy DTMF Security API handlers (3 handlers)

    - Move get_dtmf_security_handler, post_dtmf_security_handler, get_dtmf_logs_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.9 Copy Hardware Test API handlers (6 handlers)

    - Move post_hardware_test_doorbell_handler, post_hardware_test_door_handler
    - Move post_hardware_test_light_handler, get_hardware_state_handler
    - Move post_hardware_test_stop_handler
    - _Requirements: 1.2, 5.1_
  

  - [x] 3.10 Copy Certificate Management API handlers (5 handlers)

    - Move get_cert_info_handler, post_cert_upload_handler, post_cert_generate_handler
    - Move get_cert_download_handler, delete_cert_handler
    - _Requirements: 1.2, 5.1_
  
  - [x] 3.11 Copy Authentication API handlers (5 handlers)


    - Move post_auth_login_handler, post_auth_logout_handler
    - Move post_auth_set_password_handler, post_auth_change_password_handler
    - Move get_auth_logs_handler
    - _Requirements: 1.2, 5.1_

- [x] 4. Move URI handler structures to web_api.c



  - [x] 4.1 Copy all SIP URI structures (8 structures)


    - Move sip_status_uri, sip_config_get_uri, sip_config_post_uri, sip_test_uri
    - Move sip_test_call_uri, sip_log_uri, sip_connect_uri, sip_disconnect_uri
    - _Requirements: 1.2, 5.2_
  

  - [x] 4.2 Copy all WiFi URI structures (5 structures)

    - Move wifi_config_get_uri, wifi_config_post_uri, wifi_status_uri
    - Move wifi_scan_uri, wifi_connect_uri
    - _Requirements: 1.2, 5.2_
  


  - [x] 4.3 Copy all other API URI structures (25 structures)

    - Move network_ip_uri, email_config_get_uri, email_config_post_uri
    - Move ota_version_uri, system_status_uri, system_restart_uri, system_info_uri
    - Move ntp_status_uri, ntp_config_get_uri, ntp_config_post_uri, ntp_sync_uri
    - Move dtmf_security_get_uri, dtmf_security_post_uri, dtmf_logs_uri
    - Move hardware_test_doorbell_uri, hardware_test_door_uri, hardware_test_light_uri
    - Move hardware_state_uri, hardware_status_uri, hardware_test_stop_uri
    - Move cert_info_uri, cert_upload_uri, cert_generate_uri, cert_download_uri, cert_delete_uri
    - Move auth_login_uri, auth_logout_uri, auth_set_password_uri
    - Move auth_change_password_uri, auth_logs_uri
    - _Requirements: 1.2, 5.2_

- [x] 5. Implement API handler registration function






  - [x] 5.1 Create web_api_register_handlers function in web_api.c

    - Accept httpd_handle_t server parameter
    - Call httpd_register_uri_handler for all 43 API endpoints
    - Add logging for registration status
    - _Requirements: 1.4, 1.5_
  

  - [x] 5.2 Add function declaration to web_api.h

    - Declare void web_api_register_handlers(httpd_handle_t server)
    - Add include guard and necessary includes
    - _Requirements: 1.4, 6.1_

- [x] 6. Update web_server.c to use new API module




  - [x] 6.1 Remove all API handler functions from web_server.c

    - Delete all 43 API handler function implementations
    - Keep only HTML page handlers (index, documentation, login, setup)
    - _Requirements: 1.1, 1.3, 4.1_
  

  - [x] 6.2 Remove all API URI structures from web_server.c

    - Delete all 43 API URI handler structures
    - Keep only HTML page URI structures (root_uri, documentation_uri, login_uri, setup_uri)
    - _Requirements: 1.1, 1.3, 4.1_

  

  - [x] 6.3 Remove email configuration code from web_server.c
    - Delete email_config_t structure definition
    - Delete email_save_config and email_load_config functions

    - _Requirements: 3.1, 4.1_

  
  - [x] 6.4 Make auth_filter function non-static and add to header
    - Change auth_filter from static to non-static in web_server.c
    - Add esp_err_t auth_filter(httpd_req_t *req) declaration to web_server.h


    - _Requirements: 2.1, 2.2, 6.3_
  
  - [x] 6.5 Update web_server_start to call API registration
    - Add #include "web_api.h" to web_server.c
    - Call web_api_register_handlers(server) after HTML handler registration


    - Verify registration happens before server is fully started
    - _Requirements: 1.5, 6.4_
  
  - [x] 6.6 Remove API handler registrations from web_server_start

    - Delete all httpd_register_uri_handler calls for API endpoints
    - Keep only HTML page handler registrations
    - _Requirements: 1.1, 1.3_

- [x] 7. Add necessary includes to web_api.c





  - Add all required ESP-IDF headers (esp_log.h, esp_http_server.h, esp_system.h, etc.)
  - Add all component headers (sip_client.h, wifi_manager.h, ntp_sync.h, etc.)
  - Add web_server.h to access auth_filter function
  - Add cJSON.h for JSON operations
  - _Requirements: 3.3, 6.2, 6.4_

- [ ] 8. Verify compilation and resolve errors
  - Compile firmware and fix any compilation errors
  - Resolve missing includes or function declarations
  - Check for no circular dependencies between web_server and web_api
  - Verify all handler functions are properly defined
  - _Requirements: 5.5, 6.5_

- [ ] 9. Test refactored implementation
  - [ ] 9.1 Flash firmware and verify server starts
    - Flash compiled firmware to device
    - Verify HTTPS server starts on port 443
    - Verify HTTP redirect server starts on port 80
    - Check logs for successful handler registration
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.2 Test HTML page serving
    - Access / and verify index.html loads
    - Access /login.html and verify login page loads
    - Access /setup.html and verify setup page loads
    - Access /documentation.html and verify documentation loads
    - _Requirements: 1.3, 5.1_
  
  - [ ] 9.3 Test authentication flow
    - Test initial password setup via /api/auth/set-password
    - Test login via /api/auth/login
    - Test logout via /api/auth/logout
    - Test password change via /api/auth/change-password
    - Verify authentication filter works for protected endpoints
    - _Requirements: 2.3, 2.4, 2.5, 5.1, 5.4_
  
  - [ ] 9.4 Test SIP API endpoints
    - GET /api/sip/status
    - GET /api/sip/config
    - POST /api/sip/config
    - POST /api/sip/connect
    - POST /api/sip/disconnect
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.5 Test WiFi API endpoints
    - GET /api/wifi/status
    - GET /api/wifi/config
    - POST /api/wifi/scan
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.6 Test System and NTP API endpoints
    - GET /api/system/status
    - GET /api/system/info
    - GET /api/ntp/status
    - GET /api/ntp/config
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.7 Test Hardware Test API endpoints
    - GET /api/hardware/state
    - POST /api/hardware/test/doorbell
    - POST /api/hardware/test/stop
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.8 Test Certificate Management API endpoints
    - GET /api/cert/info
    - POST /api/cert/generate (if no cert exists)
    - _Requirements: 5.1, 5.2, 5.3_
  
  - [ ] 9.9 Verify no functional regressions
    - Compare API responses with pre-refactoring behavior
    - Verify all error handling works correctly
    - Confirm memory usage is acceptable
    - Check for any unexpected behavior
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_
