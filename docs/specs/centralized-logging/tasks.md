# Implementation Plan

- [ ] 1. Create system_log module with core functionality
  - Create main/system_log.h with public API definitions
  - Create main/system_log.c with circular buffer implementation
  - Implement log_init() function with mutex and buffer initialization
  - Implement log_event() function with thread-safe logging
  - Implement log_get_entries() function for retrieving logs in reverse chronological order
  - Implement log_get_count() and log_clear_all() utility functions
  - Add system_log.c to main/CMakeLists.txt
  - _Requirements: 1.1, 1.2, 2.1, 2.2, 2.3, 2.4, 2.5, 3.1, 3.2, 3.3, 3.4, 3.5, 4.1, 4.2, 4.3, 4.4, 4.5_

- [ ] 2. Implement NVS persistence for critical events
  - Add NVS namespace and key definitions for log storage
  - Implement is_critical_event() helper function to identify critical events
  - Implement persist_to_nvs() function to write critical events to NVS
  - Implement load_from_nvs() function to load persisted logs on initialization
  - Integrate NVS loading into log_init()
  - Integrate NVS persistence into log_event() for critical events
  - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

- [ ] 3. Initialize system_log in main.c
  - Add #include "system_log.h" to main/main.c
  - Call log_init() during system initialization (after NVS init, before other modules)
  - Add initial system_boot log entry
  - _Requirements: 1.1, 4.5_

- [ ] 4. Migrate auth_manager to use system_log
  - Update auth_manager.c to include system_log.h
  - Replace add_audit_log_internal() calls with log_event() calls
  - Update auth_log_security_event() to call log_event() instead of add_audit_log_internal()
  - Update auth_get_audit_logs() to call log_get_entries()
  - Remove internal audit_logs buffer, audit_log_head, and audit_log_count variables
  - Remove add_audit_log_internal() and add_audit_log() functions
  - Update auth_manager.h documentation to reference system_log
  - _Requirements: 6.1, 6.4, 6.5_

- [ ] 5. Migrate cert_manager to use system_log
  - Update cert_manager.c to include system_log.h
  - Replace cert_log_operation() implementation to call log_event()
  - Update all certificate operation functions to call cert_log_operation()
  - Verify cert_log_operation() is called for generate, upload, delete, and validate operations
  - _Requirements: 6.2, 6.4, 6.5_

- [ ] 6. Add logging to web_server operations
  - Update web_server.c to include system_log.h
  - Add log_event() calls for configuration changes (WiFi, SIP, email, etc.)
  - Add log_event() calls for certificate uploads via web interface
  - Add log_event() calls for backup/restore operations
  - Add log_event() calls for firmware update operations
  - _Requirements: 6.3, 6.4, 6.5_

- [ ] 7. Update web API to expose centralized logs
  - Update /api/logs endpoint to call log_get_entries() instead of auth_get_audit_logs()
  - Verify JSON response format matches existing format
  - Test log retrieval via web interface
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5_

- [ ]* 8. Write unit tests for system_log module
  - Test log_init() initialization
  - Test log_event() with various parameter combinations
  - Test log_event() with NULL parameters
  - Test circular buffer wraparound behavior
  - Test log_get_entries() reverse chronological order
  - Test log_get_entries() with empty buffer
  - Test log_get_entries() with partial retrieval
  - Test NVS persistence and loading
  - Test thread safety with concurrent logging
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 2.1, 2.2, 2.3, 2.4, 2.5, 3.1, 3.2, 3.3, 3.4, 3.5, 4.1, 4.2, 4.3, 4.4, 4.5, 5.1, 5.2, 5.3, 5.4, 5.5_

- [ ]* 9. Integration testing and validation
  - Verify all auth_manager events are logged correctly
  - Verify all cert_manager events are logged correctly
  - Verify all web_server events are logged correctly
  - Test log persistence across reboots
  - Verify log retrieval via web interface
  - Measure memory usage and compare with design estimates
  - Measure logging performance overhead
  - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_
