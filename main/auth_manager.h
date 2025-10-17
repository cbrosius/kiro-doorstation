#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Password hashing constants
#define AUTH_SALT_SIZE 16
#define AUTH_HASH_SIZE 32
#define AUTH_ITERATIONS 10000

// Session constants
#define AUTH_SESSION_ID_SIZE 33  // 32 hex chars + null terminator
#define AUTH_MAX_SESSIONS 5
#define AUTH_SESSION_TIMEOUT_SECONDS 1800  // 30 minutes
#define AUTH_USERNAME_MAX_LEN 32
#define AUTH_IP_ADDRESS_MAX_LEN 16
#define AUTH_ERROR_MESSAGE_MAX_LEN 128

// Login attempt tracking constants
#define AUTH_MAX_FAILED_ATTEMPTS 5
#define AUTH_FAILED_ATTEMPT_WINDOW_SECONDS 900  // 15 minutes
#define AUTH_BLOCK_DURATION_SECONDS 300  // 5 minutes

// Audit logging constants
#define AUTH_MAX_AUDIT_LOGS 100
#define AUTH_AUDIT_USERNAME_MAX_LEN 32
#define AUTH_AUDIT_IP_MAX_LEN 16
#define AUTH_AUDIT_RESULT_MAX_LEN 32

// Password hashing structure
typedef struct {
    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t hash[AUTH_HASH_SIZE];
} password_hash_t;

// Session structure
typedef struct {
    char session_id[AUTH_SESSION_ID_SIZE];
    char username[AUTH_USERNAME_MAX_LEN];
    char ip_address[AUTH_IP_ADDRESS_MAX_LEN];
    uint32_t created_at;
    uint32_t last_activity;
    uint32_t expires_at;
    bool valid;
} session_t;

// User credentials structure
typedef struct {
    char username[AUTH_USERNAME_MAX_LEN];
    password_hash_t password_hash;
    char role[16];  // "admin", "operator", "viewer"
    bool enabled;
    uint32_t created_at;
    uint32_t last_login;
} user_credentials_t;

// Login attempt tracking structure
typedef struct {
    char ip_address[AUTH_IP_ADDRESS_MAX_LEN];
    uint32_t failed_attempts;
    uint32_t last_attempt_time;
    bool blocked;
    uint32_t block_until;
} login_attempts_t;

// Authentication result structure
typedef struct {
    bool authenticated;
    char session_id[AUTH_SESSION_ID_SIZE];
    uint32_t expires_at;
    char error_message[AUTH_ERROR_MESSAGE_MAX_LEN];
} auth_result_t;

// Audit log entry structure
typedef struct {
    uint32_t timestamp;
    char username[AUTH_AUDIT_USERNAME_MAX_LEN];
    char ip_address[AUTH_AUDIT_IP_MAX_LEN];
    char result[AUTH_AUDIT_RESULT_MAX_LEN];  // "success", "failed", "blocked"
    bool success;
} audit_log_entry_t;

/**
 * @brief Initialize authentication manager
 * 
 * Initializes the authentication system, loads stored credentials,
 * and sets up session management.
 */
void auth_manager_init(void);

/**
 * @brief Authenticate user with username and password
 * 
 * @param username Username to authenticate
 * @param password Password to verify
 * @param client_ip Client IP address for rate limiting
 * @return auth_result_t Authentication result with session ID if successful
 */
auth_result_t auth_login(const char* username, const char* password, const char* client_ip);

/**
 * @brief Validate a session ID
 * 
 * @param session_id Session ID to validate
 * @return true if session is valid and not expired
 * @return false if session is invalid or expired
 */
bool auth_validate_session(const char* session_id);

/**
 * @brief Logout and invalidate a session
 * 
 * @param session_id Session ID to invalidate
 */
void auth_logout(const char* session_id);

/**
 * @brief Change user password
 * 
 * @param current_password Current password for verification
 * @param new_password New password to set
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t auth_change_password(const char* current_password, const char* new_password);

/**
 * @brief Set initial password (first boot)
 * 
 * @param password Password to set
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t auth_set_initial_password(const char* password);

/**
 * @brief Check if admin password is set
 * 
 * @return true if password is configured
 * @return false if password needs to be set
 */
bool auth_is_password_set(void);

/**
 * @brief Cleanup expired sessions
 * 
 * Should be called periodically to remove expired sessions
 */
void auth_cleanup_expired_sessions(void);

/**
 * @brief Extend session timeout on activity
 * 
 * @param session_id Session ID to extend
 */
void auth_extend_session(const char* session_id);

/**
 * @brief Check if IP address is blocked due to failed attempts
 * 
 * @param ip_address IP address to check
 * @return true if IP is currently blocked
 * @return false if IP is not blocked
 */
bool auth_is_ip_blocked(const char* ip_address);

/**
 * @brief Record a failed login attempt
 * 
 * @param ip_address IP address that failed login
 */
void auth_record_failed_attempt(const char* ip_address);

/**
 * @brief Clear failed login attempts for an IP
 * 
 * @param ip_address IP address to clear
 */
void auth_clear_failed_attempts(const char* ip_address);

/**
 * @brief Hash a password using PBKDF2
 * 
 * @param password Password to hash
 * @param output Output structure for salt and hash
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t auth_hash_password(const char* password, password_hash_t* output);

/**
 * @brief Verify a password against stored hash
 * 
 * @param password Password to verify
 * @param stored_hash Stored password hash to compare against
 * @return true if password matches
 * @return false if password does not match
 */
bool auth_verify_password(const char* password, const password_hash_t* stored_hash);

/**
 * @brief Get audit logs
 * 
 * @param logs Array to store audit log entries
 * @param max_logs Maximum number of logs to retrieve
 * @return int Number of logs retrieved
 */
int auth_get_audit_logs(audit_log_entry_t* logs, int max_logs);

#ifdef __cplusplus
}
#endif

#endif // AUTH_MANAGER_H
