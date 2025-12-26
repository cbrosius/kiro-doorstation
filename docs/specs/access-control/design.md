# Access Control - Design Document

## Overview

This feature implements IP-based access restrictions and firewall rules for the ESP32 SIP Door Station, providing network-level security through whitelisting, blacklisting, and configurable firewall rules to protect against unauthorized access and network attacks.

## Architecture

### High-Level Architecture

```
┌─────────────────┐
│  Network Client │
└────────┬────────┘
         │ TCP/IP Connection
         ▼
┌─────────────────┐
│ Access Control  │
│    Filter       │
│  - IP Check     │
│  - Rule Match   │
│  - Rate Limit   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Web Server /   │
│  SIP Client     │
└─────────────────┘
```

### Access Control Flow

```
Connection → Extract IP → Check Blacklist → Check Whitelist → Check Rules → Rate Limit → Allow/Block
                ↓              ↓                 ↓               ↓             ↓
            Log Entry      Block if found   Block if not    Apply rule    Block if exceeded
```

## Components and Interfaces

### 1. Access Control Manager Module (`access_control.c/h`)

**Purpose**: Manages IP whitelists, blacklists, firewall rules, and access enforcement.

**Public API**:
```c
// Initialize access control system
void access_control_init(void);

// IP List Management
typedef struct {
    char ip_address[16];
    uint8_t cidr_prefix;  // 0-32, 0 means single IP
    bool is_range;
    char description[64];
    uint32_t added_at;
    bool auto_added;  // For automatic blacklisting
} ip_entry_t;

esp_err_t access_control_whitelist_add(const char* ip, uint8_t cidr, const char* description);
esp_err_t access_control_whitelist_remove(const char* ip);
bool access_control_whitelist_contains(const char* ip);
int access_control_whitelist_get_all(ip_entry_t* entries, int max_entries);

esp_err_t access_control_blacklist_add(const char* ip, uint8_t cidr, const char* description, bool auto_added);
esp_err_t access_control_blacklist_remove(const char* ip);
bool access_control_blacklist_contains(const char* ip);
int access_control_blacklist_get_all(ip_entry_t* entries, int max_entries);

// Whitelist Mode
void access_control_set_whitelist_mode(bool enabled);
bool access_control_get_whitelist_mode(void);

// Access Check
typedef enum {
    ACCESS_ALLOWED,
    ACCESS_BLOCKED_BLACKLIST,
    ACCESS_BLOCKED_WHITELIST,
    ACCESS_BLOCKED_RULE,
    ACCESS_BLOCKED_RATE_LIMIT
} access_decision_t;

access_decision_t access_control_check(const char* ip, uint16_t port, const char* protocol);

// Firewall Rules
typedef struct {
    uint16_t port_start;
    uint16_t port_end;
    char protocol[4];  // "TCP", "UDP", "ALL"
    char source_ip[16];
    uint8_t source_cidr;
    bool allow;  // true = allow, false = block
    uint8_t priority;  // 0-255, lower = higher priority
    char description[64];
} firewall_rule_t;

esp_err_t access_control_rule_add(const firewall_rule_t* rule);
esp_err_t access_control_rule_remove(int rule_id);
int access_control_rule_get_all(firewall_rule_t* rules, int max_rules);

// Rate Limiting
typedef struct {
    char ip_address[16];
    uint32_t request_count;
    uint32_t window_start;
    uint32_t block_until;
    bool blocked;
} rate_limit_entry_t;

bool access_control_rate_limit_check(const char* ip);
void access_control_rate_limit_record(const char* ip);

// Access Logging
typedef struct {
    uint32_t timestamp;
    char ip_address[16];
    uint16_t port;
    char protocol[4];
    access_decision_t decision;
    char reason[64];
} access_log_entry_t;

int access_control_get_logs(access_log_entry_t* logs, int max_logs, uint32_t since_timestamp);

// Intrusion Detection
void access_control_record_failed_login(const char* ip);
void access_control_record_connection_attempt(const char* ip);

// Configuration
typedef struct {
    bool whitelist_mode;
    bool trust_local_network;
    bool auto_blacklist_enabled;
    uint32_t rate_limit_requests;
    uint32_t rate_limit_window;
    char default_policy[8];  // "allow" or "deny"
} access_control_config_t;

esp_err_t access_control_set_config(const access_control_config_t* config);
esp_err_t access_control_get_config(access_control_config_t* config);
```

### 2. IP Address Utilities

**IP Parsing and Matching**:
```c
// Parse IP address string to uint32_t
uint32_t ip_string_to_uint32(const char* ip_str);

// Parse CIDR notation
bool parse_cidr(const char* cidr_str, uint32_t* ip, uint8_t* prefix);

// Check if IP is in range
bool ip_in_range(uint32_t ip, uint32_t network, uint8_t prefix);

// Get local network subnet
bool get_local_subnet(uint32_t* network, uint8_t* prefix);
```

### 3. HTTP Server Integration

**Connection Filter**:
```c
// HTTP server connection callback
static esp_err_t http_connection_handler(httpd_handle_t hd, int sockfd) {
    // Get client IP address
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(sockfd, (struct sockaddr*)&addr, &addr_len);
    
    char client_ip[16];
    inet_ntoa_r(addr.sin_addr, client_ip, sizeof(client_ip));
    
    // Check access control
    access_decision_t decision = access_control_check(client_ip, 80, "TCP");
    
    if (decision != ACCESS_ALLOWED) {
        ESP_LOGW(TAG, "Connection blocked from %s: %d", client_ip, decision);
        close(sockfd);
        return ESP_FAIL;
    }
    
    // Check rate limiting
    if (!access_control_rate_limit_check(client_ip)) {
        ESP_LOGW(TAG, "Rate limit exceeded for %s", client_ip);
        close(sockfd);
        return ESP_FAIL;
    }
    
    access_control_rate_limit_record(client_ip);
    
    return ESP_OK;
}
```

## Data Models

### IP List Storage
```c
#define MAX_WHITELIST_ENTRIES 50
#define MAX_BLACKLIST_ENTRIES 100

typedef struct {
    ip_entry_t whitelist[MAX_WHITELIST_ENTRIES];
    int whitelist_count;
    ip_entry_t blacklist[MAX_BLACKLIST_ENTRIES];
    int blacklist_count;
} ip_lists_t;
```

### Firewall Rules Storage
```c
#define MAX_FIREWALL_RULES 20

typedef struct {
    firewall_rule_t rules[MAX_FIREWALL_RULES];
    int rule_count;
} firewall_rules_t;
```

### Rate Limiting Storage
```c
#define MAX_RATE_LIMIT_ENTRIES 100

typedef struct {
    rate_limit_entry_t entries[MAX_RATE_LIMIT_ENTRIES];
    int entry_count;
} rate_limit_table_t;
```

### Access Log Storage
```c
#define MAX_ACCESS_LOG_ENTRIES 200

typedef struct {
    access_log_entry_t logs[MAX_ACCESS_LOG_ENTRIES];
    int log_count;
    int log_index;  // Circular buffer index
} access_log_t;
```

## Error Handling

### Validation Errors
```c
#define AC_ERR_INVALID_IP        "Invalid IP address format"
#define AC_ERR_INVALID_CIDR      "Invalid CIDR notation"
#define AC_ERR_DUPLICATE_ENTRY   "IP already exists in list"
#define AC_ERR_LIST_FULL         "IP list is full"
#define AC_ERR_NOT_FOUND         "IP not found in list"
#define AC_ERR_INVALID_RULE      "Invalid firewall rule"
```

## Testing Strategy

### Unit Tests
1. **IP Parsing Tests**
   - Test IP string to uint32 conversion
   - Test CIDR parsing
   - Test IP range matching

2. **Access Control Tests**
   - Test whitelist/blacklist checking
   - Test firewall rule matching
   - Test rate limiting

### Integration Tests
1. **Connection Tests**
   - Test allowed connections
   - Test blocked connections
   - Test rate limiting enforcement

## Security Considerations

### Default Configuration
- Whitelist mode: Disabled by default
- Trust local network: Enabled by default
- Auto-blacklist: Enabled by default
- Rate limit: 100 requests per minute per IP

### Performance Impact
- IP check: <1ms per connection
- Rule evaluation: <5ms per connection
- Memory usage: ~20KB for all lists and logs

## Implementation Notes

### NVS Storage Schema
```c
// NVS keys
#define NVS_KEY_WHITELIST "ac_whitelist"
#define NVS_KEY_BLACKLIST "ac_blacklist"
#define NVS_KEY_RULES     "ac_rules"
#define NVS_KEY_CONFIG    "ac_config"

// Save/load functions
esp_err_t access_control_save_to_nvs(void);
esp_err_t access_control_load_from_nvs(void);
```

### Web Interface Design

```html
<div class="access-control-section">
    <h3>Access Control</h3>
    
    <div class="whitelist-section">
        <h4>IP Whitelist</h4>
        <label>
            <input type="checkbox" id="whitelist-mode"> Enable Whitelist Mode
        </label>
        <table id="whitelist-table">
            <thead>
                <tr>
                    <th>IP Address/Range</th>
                    <th>Description</th>
                    <th>Added</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody></tbody>
        </table>
        <button onclick="addWhitelistEntry()">Add IP</button>
    </div>
    
    <div class="blacklist-section">
        <h4>IP Blacklist</h4>
        <table id="blacklist-table">
            <thead>
                <tr>
                    <th>IP Address/Range</th>
                    <th>Description</th>
                    <th>Auto-Added</th>
                    <th>Added</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody></tbody>
        </table>
        <button onclick="addBlacklistEntry()">Add IP</button>
    </div>
    
    <div class="access-logs">
        <h4>Access Logs</h4>
        <table id="access-log-table">
            <thead>
                <tr>
                    <th>Time</th>
                    <th>IP Address</th>
                    <th>Port</th>
                    <th>Decision</th>
                    <th>Reason</th>
                </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>
</div>
```

## Dependencies

### ESP-IDF Components
- `lwip`: TCP/IP stack for IP address handling
- `nvs_flash`: Configuration storage

### Project Components
- `web_server`: HTTP server integration
- `auth_manager`: Integration with authentication

## Rollout Plan

### Phase 1: Basic IP Lists (MVP)
- Implement whitelist/blacklist management
- Add access control checking
- Create web interface for IP management

### Phase 2: Firewall Rules
- Implement firewall rule engine
- Add rule configuration interface
- Add access logging

### Phase 3: Advanced Features
- Add rate limiting
- Implement intrusion detection
- Add automatic blacklisting
- Add geo-blocking (future)

## References

- [CIDR Notation](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing)
- [Firewall Best Practices](https://www.cisco.com/c/en/us/support/docs/security/ios-firewall/23602-confaccesslists.html)
- [Rate Limiting Algorithms](https://en.wikipedia.org/wiki/Rate_limiting)
