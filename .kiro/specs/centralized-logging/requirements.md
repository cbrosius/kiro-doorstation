# Requirements Document

## Introduction

This document specifies the requirements for a centralized logging system for the SIP doorbell firmware. Currently, security and operational logging is duplicated across multiple modules (auth_manager, cert_manager, web_server). The centralized logging system will provide a unified interface for logging security events, operational events, and audit trails across all system components.

## Glossary

- **Logging System**: The centralized module responsible for collecting, storing, and retrieving log entries from all system components
- **Audit Log**: A security-focused log entry that records authentication, authorization, and security-related events
- **Security Event**: Any event related to system security including login attempts, certificate operations, configuration changes, and access control
- **Log Entry**: A single record in the logging system containing timestamp, event type, user, IP address, result, and success status
- **Circular Buffer**: A fixed-size buffer that overwrites oldest entries when full, used for storing logs in memory
- **NVS**: Non-Volatile Storage, ESP32's persistent storage mechanism

## Requirements

### Requirement 1

**User Story:** As a system administrator, I want all security and operational events logged to a central location, so that I can monitor system activity from a single interface

#### Acceptance Criteria

1. THE Logging System SHALL provide a unified API for logging events from any system component
2. THE Logging System SHALL store log entries in a circular buffer with configurable maximum size
3. THE Logging System SHALL persist critical security events to NVS for survival across reboots
4. THE Logging System SHALL support multiple event types including authentication, certificate operations, configuration changes, and system events
5. THE Logging System SHALL include timestamp, event type, username, IP address, result message, and success status in each log entry

### Requirement 2

**User Story:** As a developer, I want a simple logging API that replaces the current auth_log_security_event function, so that I can easily log events without duplicating code

#### Acceptance Criteria

1. THE Logging System SHALL provide a function with signature matching auth_log_security_event for backward compatibility
2. THE Logging System SHALL accept NULL values for optional parameters (username, IP address) without errors
3. THE Logging System SHALL automatically capture timestamps using the system time
4. THE Logging System SHALL validate input parameters and truncate strings that exceed maximum lengths
5. THE Logging System SHALL log to ESP_LOG in addition to storing in the audit buffer

### Requirement 3

**User Story:** As a system administrator, I want to retrieve logs in reverse chronological order, so that I can see the most recent events first

#### Acceptance Criteria

1. THE Logging System SHALL provide a function to retrieve logs with newest entries first
2. THE Logging System SHALL support retrieving a specified number of log entries
3. THE Logging System SHALL return the actual number of entries retrieved
4. WHEN the requested count exceeds available logs, THE Logging System SHALL return all available logs
5. THE Logging System SHALL handle empty log buffers gracefully without errors

### Requirement 4

**User Story:** As a system component, I want the logging system to be thread-safe, so that multiple tasks can log events concurrently without corruption

#### Acceptance Criteria

1. THE Logging System SHALL use FreeRTOS mutexes to protect shared log buffer access
2. THE Logging System SHALL acquire mutex locks before modifying the circular buffer
3. THE Logging System SHALL release mutex locks after completing buffer operations
4. THE Logging System SHALL handle mutex acquisition failures gracefully
5. THE Logging System SHALL initialize mutexes during system initialization

### Requirement 5

**User Story:** As a system administrator, I want critical security events persisted to NVS, so that I can review security incidents even after device reboots

#### Acceptance Criteria

1. WHEN a critical security event occurs, THE Logging System SHALL write the event to NVS
2. THE Logging System SHALL define critical events as failed login attempts, password changes, certificate operations, and configuration changes
3. THE Logging System SHALL store up to 50 critical events in NVS
4. THE Logging System SHALL load persisted events from NVS during initialization
5. THE Logging System SHALL merge NVS-persisted events with in-memory logs for retrieval

### Requirement 6

**User Story:** As a developer, I want to migrate existing logging calls to the centralized system, so that all components use consistent logging

#### Acceptance Criteria

1. THE Logging System SHALL replace auth_log_security_event calls in auth_manager
2. THE Logging System SHALL replace cert_log_operation calls in cert_manager
3. THE Logging System SHALL provide logging for web_server operations
4. THE Logging System SHALL maintain backward compatibility with existing log formats
5. THE Logging System SHALL preserve all existing log information during migration
