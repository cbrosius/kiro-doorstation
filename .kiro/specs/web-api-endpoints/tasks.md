# Implementation Plan

- [x] 1. Implement WiFi configuration endpoint


  - Add GET handler to retrieve current WiFi SSID from wifi_manager
  - Add POST handler to save WiFi configuration using existing wifi_save_config()
  - Register both GET and POST URI handlers for /api/wifi/config
  - Validate JSON input and return appropriate error codes
  - _Requirements: 1.1, 1.2, 1.3, 1.4_

- [x] 2. Implement network IP information endpoint


  - Add GET handler to retrieve IP configuration using esp_netif_get_ip_info()
  - Retrieve DNS information using esp_netif_get_dns_info()
  - Format response with IP address, netmask, gateway, and DNS
  - Handle disconnected state with empty/null values
  - Register URI handler for /api/network/ip
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

- [x] 3. Implement email configuration storage and retrieval


- [x] 3.1 Create email configuration data structure and NVS functions


  - Define email_config_t structure with SMTP settings
  - Implement email_save_config() to persist configuration to NVS
  - Implement email_load_config() to retrieve configuration from NVS
  - Use NVS namespace "email_config" with appropriate keys
  - _Requirements: 3.1, 3.2_

- [x] 3.2 Implement email configuration endpoint handlers


  - Add GET handler to retrieve email configuration without password
  - Add POST handler to save email configuration with validation
  - Validate SMTP server format, port range, and email format
  - Register both GET and POST URI handlers for /api/email/config
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [x] 4. Implement OTA version information endpoint


  - Add GET handler using esp_ota_get_app_description()
  - Retrieve firmware version, build date, and IDF version
  - Calculate OTA partition size and available space
  - Format response with comprehensive version information
  - Register URI handler for /api/ota/version
  - _Requirements: 4.1, 4.2, 4.3, 4.4_

- [x] 5. Implement system information endpoint


  - Add GET handler using esp_chip_info() for chip details
  - Retrieve flash size using spi_flash_get_chip_size()
  - Get MAC address using esp_efuse_mac_get_default()
  - Include CPU cores, frequency, and memory information
  - Combine with existing system status data (uptime, heap)
  - Register URI handler for /api/system/info
  - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

- [x] 6. Update web server configuration and registration



  - Increase max_uri_handlers count to accommodate new endpoints
  - Register all new URI handlers in web_server_start()
  - Verify all endpoints are properly registered
  - _Requirements: All_
