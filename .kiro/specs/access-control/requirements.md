# Requirements Document

## Introduction

The Access Control feature implements IP-based access restrictions and firewall rules for the ESP32 SIP Door Station. This feature provides network-level security by allowing administrators to whitelist trusted IP addresses, blacklist malicious IPs, and configure firewall rules to protect the device from unauthorized network access.

## Glossary

- **System**: The ESP32 SIP Door Station access control system
- **IP_Whitelist**: List of IP addresses or ranges allowed to access the device
- **IP_Blacklist**: List of IP addresses or ranges blocked from accessing the device
- **Firewall_Rule**: Network access rule defining allowed/blocked traffic
- **CIDR_Notation**: IP address range notation (e.g., 192.168.1.0/24)
- **Access_Log**: Record of connection attempts and access decisions
- **Rate_Limiting**: Restriction on number of requests per time period

## Requirements

### Requirement 1: IP Whitelist Management

**User Story:** As a system administrator, I want to create an IP whitelist, so that only trusted devices can access the door station.

#### Acceptance Criteria

1. WHEN THE administrator enables whitelist mode, THE System SHALL block all IP addresses not on the whitelist
2. WHEN administrator adds IP address to whitelist, THE System SHALL validate IP format
3. WHEN IP address is validated, THE System SHALL add IP to whitelist
4. WHEN administrator adds IP range in CIDR notation, THE System SHALL validate CIDR format
5. WHEN CIDR is validated, THE System SHALL add range to whitelist
6. WHEN whitelist is updated, THE System SHALL save changes to NVS
7. WHEN whitelist mode is disabled, THE System SHALL allow all IP addresses except blacklisted ones

### Requirement 2: IP Blacklist Management

**User Story:** As a system administrator, I want to blacklist malicious IP addresses, so that known attackers cannot access the device.

#### Acceptance Criteria

1. WHEN THE administrator adds IP to blacklist, THE System SHALL validate IP format
2. WHEN IP is validated, THE System SHALL add IP to blacklist
3. WHEN IP is blacklisted, THE System SHALL immediately block all connections from that IP
4. WHEN administrator adds IP range to blacklist, THE System SHALL validate CIDR format
5. WHEN CIDR is validated, THE System SHALL add range to blacklist
6. WHEN blacklist is updated, THE System SHALL save changes to NVS

### Requirement 3: Access Control Enforcement

**User Story:** As a system administrator, I want automatic enforcement of access rules, so that unauthorized IPs are blocked without manual intervention.

#### Acceptance Criteria

1. WHEN THE client attempts connection, THE System SHALL extract client IP address
2. WHEN IP is extracted, THE System SHALL check if IP is blacklisted
3. IF IP is blacklisted, THEN THE System SHALL reject connection immediately
4. WHEN IP is not blacklisted, THE System SHALL check if whitelist mode is enabled
5. IF whitelist mode is enabled AND IP is not whitelisted, THEN THE System SHALL reject connection
6. WHEN connection is rejected, THE System SHALL log rejection with IP address and timestamp
7. WHEN connection is allowed, THE System SHALL log access with IP address and timestamp

### Requirement 4: Firewall Rule Configuration

**User Story:** As a system administrator, I want to configure firewall rules for different services, so that I can control access to specific ports and protocols.

#### Acceptance Criteria

1. WHEN THE administrator creates firewall rule, THE System SHALL require port number or range
2. WHEN rule is created, THE System SHALL require protocol (TCP/UDP/Both)
3. WHEN rule is created, THE System SHALL require action (Allow/Block)
4. WHEN rule is created, THE System SHALL optionally accept source IP or range
5. WHEN rule is saved, THE System SHALL validate rule does not conflict with existing rules
6. WHEN rules are updated, THE System SHALL apply rules in priority order
7. WHEN no rule matches, THE System SHALL apply default policy (allow or block)

### Requirement 5: Access Logging

**User Story:** As a system administrator, I want detailed access logs, so that I can monitor connection attempts and identify security threats.

#### Acceptance Criteria

1. WHEN THE client attempts connection, THE System SHALL log attempt with timestamp, IP, port, and protocol
2. WHEN connection is allowed, THE System SHALL log "ALLOW" decision with matching rule
3. WHEN connection is blocked, THE System SHALL log "BLOCK" decision with reason
4. WHEN log buffer is full, THE System SHALL rotate logs (keep last 200 entries)
5. WHEN administrator views logs, THE System SHALL display logs in reverse chronological order

### Requirement 6: Automatic Blacklist (Intrusion Detection)

**User Story:** As a system administrator, I want automatic blacklisting of suspicious IPs, so that the system can defend itself against attacks.

#### Acceptance Criteria

1. WHEN THE System detects 10 failed login attempts from same IP within 5 minutes, THE System SHALL automatically add IP to blacklist
2. WHEN System detects 50 connection attempts from same IP within 1 minute, THE System SHALL automatically add IP to blacklist
3. WHEN IP is auto-blacklisted, THE System SHALL log event with reason
4. WHEN IP is auto-blacklisted, THE System SHALL send notification (if configured)
5. WHEN administrator views blacklist, THE System SHALL indicate which entries are automatic

### Requirement 7: Whitelist/Blacklist Import/Export

**User Story:** As a system administrator, I want to import and export access control lists, so that I can share lists between devices or backup configurations.

#### Acceptance Criteria

1. WHEN THE administrator exports whitelist, THE System SHALL generate JSON file with all whitelisted IPs
2. WHEN administrator exports blacklist, THE System SHALL generate JSON file with all blacklisted IPs
3. WHEN administrator imports whitelist, THE System SHALL validate JSON format
4. WHEN JSON is validated, THE System SHALL merge imported IPs with existing whitelist
5. WHEN import completes, THE System SHALL display count of added entries

### Requirement 8: Geographic IP Blocking (Future)

**User Story:** As a system administrator, I want to block entire countries, so that I can prevent access from high-risk regions.

#### Acceptance Criteria

1. WHEN THE administrator enables geo-blocking, THE System SHALL load country IP ranges
2. WHEN country is selected for blocking, THE System SHALL add all country IP ranges to blacklist
3. WHEN client connects, THE System SHALL check if IP belongs to blocked country
4. IF IP is from blocked country, THEN THE System SHALL reject connection
5. WHEN geo-blocking is updated, THE System SHALL save configuration to NVS

### Requirement 9: Rate Limiting per IP

**User Story:** As a system administrator, I want rate limiting per IP address, so that no single client can overwhelm the device with requests.

#### Acceptance Criteria

1. WHEN THE System receives request, THE System SHALL track request count per IP address
2. WHEN IP exceeds 100 requests per minute, THE System SHALL temporarily block IP for 1 minute
3. WHEN IP is rate-limited, THE System SHALL return 429 Too Many Requests status
4. WHEN rate limit period expires, THE System SHALL automatically unblock IP
5. WHEN IP is repeatedly rate-limited, THE System SHALL increase block duration exponentially

### Requirement 10: Web Interface Access Control Section

**User Story:** As a system administrator, I want a dedicated web interface section for access control, so that I can easily manage IP lists and firewall rules.

#### Acceptance Criteria

1. WHEN THE web interface loads, THE System SHALL display "Access Control" menu item
2. WHEN section is displayed, THE System SHALL show whitelist enable/disable toggle
3. WHEN section is displayed, THE System SHALL show list of whitelisted IPs with add/remove buttons
4. WHEN section is displayed, THE System SHALL show list of blacklisted IPs with add/remove buttons
5. WHEN section is displayed, THE System SHALL show firewall rules table with add/edit/delete buttons
6. WHEN section is displayed, THE System SHALL show access log viewer with filter options

### Requirement 11: API Endpoints for Access Control

**User Story:** As a developer, I want RESTful API endpoints for access control management, so that the web interface can manage IP lists and rules.

#### Acceptance Criteria

1. WHEN THE System starts, THE System SHALL register GET /api/access/whitelist endpoint
2. WHEN THE System starts, THE System SHALL register POST /api/access/whitelist endpoint
3. WHEN THE System starts, THE System SHALL register DELETE /api/access/whitelist/{ip} endpoint
4. WHEN THE System starts, THE System SHALL register GET /api/access/blacklist endpoint
5. WHEN THE System starts, THE System SHALL register POST /api/access/blacklist endpoint
6. WHEN THE System starts, THE System SHALL register DELETE /api/access/blacklist/{ip} endpoint
7. WHEN THE System starts, THE System SHALL register GET /api/access/rules endpoint
8. WHEN THE System starts, THE System SHALL register POST /api/access/rules endpoint
9. WHEN THE System starts, THE System SHALL register GET /api/access/logs endpoint

### Requirement 12: Default Access Policies

**User Story:** As a system administrator, I want configurable default policies, so that I can choose between allow-all or deny-all approaches.

#### Acceptance Criteria

1. WHEN THE System initializes, THE System SHALL load default policy from configuration
2. WHEN default policy is "allow", THE System SHALL allow all connections except blacklisted
3. WHEN default policy is "deny", THE System SHALL block all connections except whitelisted
4. WHEN administrator changes default policy, THE System SHALL save to NVS
5. WHEN default policy changes, THE System SHALL apply immediately to new connections

### Requirement 13: Trusted Network Detection

**User Story:** As a system administrator, I want automatic detection of local network, so that local devices are not accidentally blocked.

#### Acceptance Criteria

1. WHEN THE System connects to WiFi, THE System SHALL detect local network subnet
2. WHEN local subnet is detected, THE System SHALL automatically whitelist entire subnet
3. WHEN administrator enables "Trust Local Network" option, THE System SHALL apply local subnet whitelist
4. WHEN administrator disables option, THE System SHALL remove automatic local subnet whitelist
5. WHEN local network changes, THE System SHALL update automatic whitelist

### Requirement 14: Access Control Bypass for Emergency

**User Story:** As a system administrator, I want an emergency bypass mechanism, so that I can regain access if I accidentally lock myself out.

#### Acceptance Criteria

1. WHEN THE physical reset button is pressed for 5 seconds, THE System SHALL disable all access control for 10 minutes
2. WHEN bypass is activated, THE System SHALL log bypass event
3. WHEN bypass is active, THE System SHALL display warning on web interface
4. WHEN bypass period expires, THE System SHALL re-enable access control
5. WHEN administrator manually disables bypass, THE System SHALL immediately re-enable access control
