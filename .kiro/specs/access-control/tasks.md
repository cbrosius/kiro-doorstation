# Implementation Plan

- [ ] 1. Create access control manager module
  - Create `main/access_control.h` and `main/access_control.c` files
  - Define IP entry structure and access decision enum
  - Implement `access_control_init()` function
  - Define configuration structure
  - _Requirements: 1.1, 2.1, 3.1_

- [ ] 2. Implement IP address utility functions
  - Implement `ip_string_to_uint32()` to convert IP string to integer
  - Implement `parse_cidr()` to parse CIDR notation
  - Implement `ip_in_range()` to check if IP is in CIDR range
  - Add IP format validation
  - _Requirements: 1.2, 1.4, 2.2, 2.4_

- [ ] 3. Implement IP whitelist management
  - Implement `access_control_whitelist_add()` with validation
  - Implement `access_control_whitelist_remove()` to remove entries
  - Implement `access_control_whitelist_contains()` to check if IP is whitelisted
  - Implement `access_control_whitelist_get_all()` to retrieve all entries
  - Store whitelist in NVS
  - _Requirements: 1.2, 1.3, 1.4, 1.5, 1.6_

- [ ] 4. Implement IP blacklist management
  - Implement `access_control_blacklist_add()` with validation
  - Implement `access_control_blacklist_remove()` to remove entries
  - Implement `access_control_blacklist_contains()` to check if IP is blacklisted
  - Implement `access_control_blacklist_get_all()` to retrieve all entries
  - Store blacklist in NVS
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6_

- [ ] 5. Implement whitelist mode toggle
  - Implement `access_control_set_whitelist_mode()` to enable/disable whitelist mode
  - Implement `access_control_get_whitelist_mode()` to check current mode
  - Save whitelist mode setting to NVS
  - _Requirements: 1.1, 1.7, 12.2, 12.3_

- [ ] 6. Implement access control checking
  - Implement `access_control_check()` to evaluate access decision
  - Check blacklist first (highest priority)
  - Check whitelist if whitelist mode is enabled
  - Return appropriate access decision enum
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ] 7. Implement access logging
  - Define access log entry structure
  - Implement circular buffer for access logs (200 entries)
  - Log all connection attempts with timestamp, IP, port, decision
  - Implement `access_control_get_logs()` to retrieve logs
  - _Requirements: 3.6, 3.7, 5.1, 5.2, 5.3, 5.4, 5.5_

- [ ] 8. Implement rate limiting per IP
  - Create rate limit tracking structure
  - Implement `access_control_rate_limit_check()` to check if IP is rate-limited
  - Implement `access_control_rate_limit_record()` to track requests
  - Block IP for 1 minute after exceeding 100 requests per minute
  - Implement automatic unblock after timeout
  - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

- [ ] 9. Implement automatic blacklisting (intrusion detection)
  - Implement `access_control_record_failed_login()` to track failed logins
  - Implement `access_control_record_connection_attempt()` to track connections
  - Auto-blacklist IP after 10 failed logins within 5 minutes
  - Auto-blacklist IP after 50 connection attempts within 1 minute
  - Mark auto-blacklisted entries with flag
  - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

- [ ] 10. Implement firewall rule management
  - Define firewall rule structure
  - Implement `access_control_rule_add()` with validation
  - Implement `access_control_rule_remove()` to delete rules
  - Implement `access_control_rule_get_all()` to retrieve all rules
  - Store rules in NVS
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

- [ ] 11. Integrate access control with HTTP server
  - Add connection callback to HTTP server configuration
  - Extract client IP address from socket
  - Call `access_control_check()` before accepting connection
  - Close connection if access is blocked
  - Check rate limiting before processing request
  - _Requirements: 3.1, 3.2, 3.3, 3.5_

- [ ] 12. Implement trusted local network detection
  - Implement `get_local_subnet()` to detect local network
  - Auto-whitelist local subnet when "Trust Local Network" is enabled
  - Update whitelist when WiFi network changes
  - Add configuration option to enable/disable trusted network
  - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5_

- [ ] 13. Create access control API endpoints
  - Add `GET /api/access/whitelist` endpoint to retrieve whitelist
  - Add `POST /api/access/whitelist` endpoint to add IP to whitelist
  - Add `DELETE /api/access/whitelist/{ip}` endpoint to remove IP
  - Add `GET /api/access/blacklist` endpoint to retrieve blacklist
  - Add `POST /api/access/blacklist` endpoint to add IP to blacklist
  - Add `DELETE /api/access/blacklist/{ip}` endpoint to remove IP
  - Add `GET /api/access/config` endpoint to get configuration
  - Add `POST /api/access/config` endpoint to update configuration
  - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_

- [ ] 14. Create firewall rule API endpoints
  - Add `GET /api/access/rules` endpoint to retrieve all rules
  - Add `POST /api/access/rules` endpoint to add new rule
  - Add `PUT /api/access/rules/{id}` endpoint to update rule
  - Add `DELETE /api/access/rules/{id}` endpoint to delete rule
  - _Requirements: 11.7, 11.8_

- [ ] 15. Create access log API endpoint
  - Add `GET /api/access/logs` endpoint to retrieve access logs
  - Support query parameter "since" for filtering by timestamp
  - Return logs in reverse chronological order
  - Include IP, port, decision, reason, and timestamp
  - _Requirements: 11.9, 5.5_

- [ ] 16. Create access control web interface section
  - Add "Access Control" menu item to sidebar navigation
  - Create whitelist management section with table and add/remove buttons
  - Create blacklist management section with table and add/remove buttons
  - Add whitelist mode toggle checkbox
  - Add "Trust Local Network" toggle checkbox
  - _Requirements: 10.1, 10.2, 10.3, 10.4_

- [ ] 17. Implement whitelist/blacklist UI functionality
  - Create JavaScript function to load and display whitelist
  - Create JavaScript function to add IP to whitelist with validation
  - Create JavaScript function to remove IP from whitelist
  - Create JavaScript function to load and display blacklist
  - Create JavaScript function to add IP to blacklist with validation
  - Create JavaScript function to remove IP from blacklist
  - Display auto-blacklisted entries with indicator
  - _Requirements: 10.3, 10.4, 6.5_

- [ ] 18. Implement firewall rules UI
  - Create firewall rules table with columns for port, protocol, action, source
  - Add "Add Rule" button and modal dialog
  - Implement rule validation (port range, protocol, CIDR)
  - Add edit and delete buttons for each rule
  - Display rules in priority order
  - _Requirements: 10.5_

- [ ] 19. Implement access log viewer UI
  - Create access log table with columns for time, IP, port, decision, reason
  - Implement auto-refresh every 5 seconds
  - Add filter options (by IP, by decision type)
  - Add "Clear Logs" button
  - Display logs in reverse chronological order
  - _Requirements: 10.6, 5.5_

- [ ] 20. Implement IP list import/export
  - Add `GET /api/access/export/whitelist` endpoint to download whitelist JSON
  - Add `GET /api/access/export/blacklist` endpoint to download blacklist JSON
  - Add `POST /api/access/import/whitelist` endpoint to upload whitelist JSON
  - Add `POST /api/access/import/blacklist` endpoint to upload blacklist JSON
  - Validate imported JSON format
  - Merge imported entries with existing lists
  - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5_

- [ ] 21. Implement emergency bypass mechanism
  - Add GPIO handler for physical reset button
  - Disable all access control when button held for 5 seconds
  - Set 10-minute bypass timer
  - Display warning banner on web interface during bypass
  - Re-enable access control after timeout or manual disable
  - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5_

- [ ] 22. Implement default policy configuration
  - Add default policy setting to configuration structure
  - Implement "allow" policy (block only blacklisted)
  - Implement "deny" policy (allow only whitelisted)
  - Add UI toggle for default policy selection
  - Save policy to NVS
  - _Requirements: 12.1, 12.2, 12.3, 12.4, 12.5_

- [ ] 23. Update CMakeLists.txt to include access control module
  - Add `access_control.c` to `main/CMakeLists.txt` SRCS list
  - Verify lwip component is linked
  - Ensure NVS component is available
  - _Requirements: 1.1_

- [ ]* 24. Add comprehensive access control logging
  - Log all whitelist/blacklist modifications
  - Log all firewall rule changes
  - Log all auto-blacklist events
  - Log all rate limiting events
  - Log emergency bypass activations
  - _Requirements: 3.6, 5.1, 6.3_

- [ ]* 25. Create integration tests for access control
  - Test whitelist add/remove operations
  - Test blacklist add/remove operations
  - Test access control enforcement
  - Test rate limiting
  - Test automatic blacklisting
  - Test firewall rule matching
  - Test emergency bypass
  - _Requirements: 1.3, 2.3, 3.5, 6.1, 9.2, 14.1_
