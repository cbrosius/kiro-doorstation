#include "auth_manager.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform.h"

static const char *TAG = "AUTH_MANAGER";

// NVS namespace for authentication data
#define AUTH_NVS_NAMESPACE "auth"
#define AUTH_NVS_PASSWORD_KEY "admin_pwd"
#define AUTH_NVS_USERNAME_KEY "admin_user"

// Active sessions storage
static session_t active_sessions[AUTH_MAX_SESSIONS];

// Login attempt tracking (max 10 IPs tracked)
#define MAX_TRACKED_IPS 10
static login_attempts_t login_attempts[MAX_TRACKED_IPS];

// Mutex for thread safety (if needed in future)
static bool auth_initialized = false;

/**
 * @brief Get current timestamp in seconds
 */
static uint32_t get_current_time(void) {
    return (uint32_t)time(NULL);
}

/**
 * @brief Generate cryptographically random session ID
 */
static void generate_session_id(char* session_id) {
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    
    // Convert to hex string
    for (int i = 0; i < 16; i++) {
        sprintf(&session_id[i * 2], "%02x", random_bytes[i]);
    }
    session_id[32] = '\0';
}

/**
 * @brief Validate password strength
 */
static bool validate_password_strength(const char* password) {
    if (!password || strlen(password) < 8) {
        return false;
    }
    
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    
    for (const char* p = password; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') has_upper = true;
        if (*p >= 'a' && *p <= 'z') has_lower = true;
        if (*p >= '0' && *p <= '9') has_digit = true;
    }
    
    return has_upper && has_lower && has_digit;
}

esp_err_t auth_hash_password(const char* password, password_hash_t* output) {
    if (!password || !output) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Generate random salt
    esp_fill_random(output->salt, AUTH_SALT_SIZE);
    
    // Hash password using PBKDF2
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        ESP_LOGE(TAG, "Failed to get MD info");
        mbedtls_md_free(&md_ctx);
        return ESP_FAIL;
    }
    
    int ret = mbedtls_md_setup(&md_ctx, md_info, 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to setup MD context: %d", ret);
        mbedtls_md_free(&md_ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                         (const unsigned char*)password,
                                         strlen(password),
                                         output->salt,
                                         AUTH_SALT_SIZE,
                                         AUTH_ITERATIONS,
                                         AUTH_HASH_SIZE,
                                         output->hash);
    
    mbedtls_md_free(&md_ctx);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 failed: %d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

bool auth_verify_password(const char* password, const password_hash_t* stored_hash) {
    if (!password || !stored_hash) {
        return false;
    }
    
    // Hash the provided password with the stored salt
    uint8_t computed_hash[AUTH_HASH_SIZE];
    
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    int ret = mbedtls_md_setup(&md_ctx, md_info, 1);
    if (ret != 0) {
        mbedtls_md_free(&md_ctx);
        return false;
    }
    
    ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                         (const unsigned char*)password,
                                         strlen(password),
                                         stored_hash->salt,
                                         AUTH_SALT_SIZE,
                                         AUTH_ITERATIONS,
                                         AUTH_HASH_SIZE,
                                         computed_hash);
    
    mbedtls_md_free(&md_ctx);
    
    if (ret != 0) {
        return false;
    }
    
    // Compare hashes using constant-time comparison
    return memcmp(computed_hash, stored_hash->hash, AUTH_HASH_SIZE) == 0;
}

void auth_manager_init(void) {
    ESP_LOGI(TAG, "Initializing authentication manager");
    
    // Initialize session storage
    memset(active_sessions, 0, sizeof(active_sessions));
    
    // Initialize login attempt tracking
    memset(login_attempts, 0, sizeof(login_attempts));
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return;
    }
    
    // Check if admin password exists
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No admin password set - initial setup required");
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Admin password found in NVS");
    } else {
        ESP_LOGE(TAG, "Error checking for password: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    
    auth_initialized = true;
    ESP_LOGI(TAG, "Authentication manager initialized");
}

bool auth_is_password_set(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }
    
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, NULL, &required_size);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && required_size == sizeof(password_hash_t));
}

esp_err_t auth_set_initial_password(const char* password) {
    if (!password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate password strength
    if (!validate_password_strength(password)) {
        ESP_LOGE(TAG, "Password does not meet strength requirements");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if password is already set
    if (auth_is_password_set()) {
        ESP_LOGE(TAG, "Password already set, use change_password instead");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Hash password
    password_hash_t hash;
    esp_err_t err = auth_hash_password(password, &hash);
    if (err != ESP_OK) {
        return err;
    }
    
    // Store in NVS
    nvs_handle_t nvs_handle;
    err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, &hash, sizeof(password_hash_t));
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, AUTH_NVS_USERNAME_KEY, "admin");
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
        }
    }
    
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Initial password set successfully");
    } else {
        ESP_LOGE(TAG, "Failed to store password: %s", esp_err_to_name(err));
    }
    
    return err;
}


esp_err_t auth_change_password(const char* current_password, const char* new_password) {
    if (!current_password || !new_password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate new password strength
    if (!validate_password_strength(new_password)) {
        ESP_LOGE(TAG, "New password does not meet strength requirements");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Load current password hash
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    password_hash_t stored_hash;
    size_t required_size = sizeof(password_hash_t);
    err = nvs_get_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, &stored_hash, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    // Verify current password
    if (!auth_verify_password(current_password, &stored_hash)) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Current password verification failed");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Hash new password
    password_hash_t new_hash;
    err = auth_hash_password(new_password, &new_hash);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    // Store new password
    err = nvs_set_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, &new_hash, sizeof(password_hash_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Password changed successfully");
        // Invalidate all sessions
        memset(active_sessions, 0, sizeof(active_sessions));
    }
    
    return err;
}

bool auth_is_ip_blocked(const char* ip_address) {
    if (!ip_address) {
        return false;
    }
    
    uint32_t current_time = get_current_time();
    
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (strcmp(login_attempts[i].ip_address, ip_address) == 0) {
            if (login_attempts[i].blocked && current_time < login_attempts[i].block_until) {
                return true;
            }
            // Unblock if time has passed
            if (login_attempts[i].blocked && current_time >= login_attempts[i].block_until) {
                login_attempts[i].blocked = false;
                login_attempts[i].failed_attempts = 0;
            }
            return false;
        }
    }
    
    return false;
}

void auth_record_failed_attempt(const char* ip_address) {
    if (!ip_address) {
        return;
    }
    
    uint32_t current_time = get_current_time();
    int empty_slot = -1;
    
    // Find existing entry or empty slot
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (strcmp(login_attempts[i].ip_address, ip_address) == 0) {
            // Check if attempts are within the window
            if (current_time - login_attempts[i].last_attempt_time > AUTH_FAILED_ATTEMPT_WINDOW_SECONDS) {
                // Reset counter if outside window
                login_attempts[i].failed_attempts = 1;
            } else {
                login_attempts[i].failed_attempts++;
            }
            
            login_attempts[i].last_attempt_time = current_time;
            
            // Block if threshold exceeded
            if (login_attempts[i].failed_attempts >= AUTH_MAX_FAILED_ATTEMPTS) {
                login_attempts[i].blocked = true;
                login_attempts[i].block_until = current_time + AUTH_BLOCK_DURATION_SECONDS;
                ESP_LOGW(TAG, "IP %s blocked due to %d failed attempts", 
                         ip_address, login_attempts[i].failed_attempts);
            }
            
            return;
        }
        
        if (empty_slot == -1 && login_attempts[i].ip_address[0] == '\0') {
            empty_slot = i;
        }
    }
    
    // Add new entry if space available
    if (empty_slot != -1) {
        strncpy(login_attempts[empty_slot].ip_address, ip_address, AUTH_IP_ADDRESS_MAX_LEN - 1);
        login_attempts[empty_slot].ip_address[AUTH_IP_ADDRESS_MAX_LEN - 1] = '\0';
        login_attempts[empty_slot].failed_attempts = 1;
        login_attempts[empty_slot].last_attempt_time = current_time;
        login_attempts[empty_slot].blocked = false;
    }
}

void auth_clear_failed_attempts(const char* ip_address) {
    if (!ip_address) {
        return;
    }
    
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (strcmp(login_attempts[i].ip_address, ip_address) == 0) {
            memset(&login_attempts[i], 0, sizeof(login_attempts_t));
            return;
        }
    }
}

auth_result_t auth_login(const char* username, const char* password, const char* client_ip) {
    auth_result_t result = {0};
    
    if (!username || !password) {
        strncpy(result.error_message, "Invalid credentials", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Check if IP is blocked
    if (client_ip && auth_is_ip_blocked(client_ip)) {
        strncpy(result.error_message, "Account temporarily locked due to failed attempts", 
                AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Load stored credentials
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        strncpy(result.error_message, "Authentication system error", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Get stored username
    char stored_username[AUTH_USERNAME_MAX_LEN] = {0};
    size_t username_size = sizeof(stored_username);
    err = nvs_get_str(nvs_handle, AUTH_NVS_USERNAME_KEY, stored_username, &username_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        strncpy(result.error_message, "Invalid username or password", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Verify username
    if (strcmp(username, stored_username) != 0) {
        nvs_close(nvs_handle);
        strncpy(result.error_message, "Invalid username or password", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Get stored password hash
    password_hash_t stored_hash;
    size_t hash_size = sizeof(password_hash_t);
    err = nvs_get_blob(nvs_handle, AUTH_NVS_PASSWORD_KEY, &stored_hash, &hash_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        strncpy(result.error_message, "Invalid username or password", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Verify password
    if (!auth_verify_password(password, &stored_hash)) {
        strncpy(result.error_message, "Invalid username or password", AUTH_ERROR_MESSAGE_MAX_LEN - 1);
        return result;
    }
    
    // Authentication successful - create session
    uint32_t current_time = get_current_time();
    
    // Find empty session slot
    int session_slot = -1;
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!active_sessions[i].valid) {
            session_slot = i;
            break;
        }
    }
    
    // If no empty slot, use oldest session
    if (session_slot == -1) {
        uint32_t oldest_time = current_time;
        for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
            if (active_sessions[i].created_at < oldest_time) {
                oldest_time = active_sessions[i].created_at;
                session_slot = i;
            }
        }
    }
    
    // Create new session
    session_t* session = &active_sessions[session_slot];
    generate_session_id(session->session_id);
    strncpy(session->username, username, AUTH_USERNAME_MAX_LEN - 1);
    session->username[AUTH_USERNAME_MAX_LEN - 1] = '\0';
    
    if (client_ip) {
        strncpy(session->ip_address, client_ip, AUTH_IP_ADDRESS_MAX_LEN - 1);
        session->ip_address[AUTH_IP_ADDRESS_MAX_LEN - 1] = '\0';
    }
    
    session->created_at = current_time;
    session->last_activity = current_time;
    session->expires_at = current_time + AUTH_SESSION_TIMEOUT_SECONDS;
    session->valid = true;
    
    // Set result
    result.authenticated = true;
    strncpy(result.session_id, session->session_id, AUTH_SESSION_ID_SIZE - 1);
    result.session_id[AUTH_SESSION_ID_SIZE - 1] = '\0';
    result.expires_at = session->expires_at;
    
    // Clear failed attempts on successful login
    if (client_ip) {
        auth_clear_failed_attempts(client_ip);
    }
    
    ESP_LOGI(TAG, "User '%s' logged in successfully from %s", username, client_ip ? client_ip : "unknown");
    
    return result;
}

bool auth_validate_session(const char* session_id) {
    if (!session_id) {
        return false;
    }
    
    uint32_t current_time = get_current_time();
    
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (active_sessions[i].valid && 
            strcmp(active_sessions[i].session_id, session_id) == 0) {
            
            // Check if session has expired
            if (current_time > active_sessions[i].expires_at) {
                active_sessions[i].valid = false;
                ESP_LOGI(TAG, "Session expired for user '%s'", active_sessions[i].username);
                return false;
            }
            
            return true;
        }
    }
    
    return false;
}

void auth_extend_session(const char* session_id) {
    if (!session_id) {
        return;
    }
    
    uint32_t current_time = get_current_time();
    
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (active_sessions[i].valid && 
            strcmp(active_sessions[i].session_id, session_id) == 0) {
            
            active_sessions[i].last_activity = current_time;
            active_sessions[i].expires_at = current_time + AUTH_SESSION_TIMEOUT_SECONDS;
            return;
        }
    }
}

void auth_logout(const char* session_id) {
    if (!session_id) {
        return;
    }
    
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (strcmp(active_sessions[i].session_id, session_id) == 0) {
            ESP_LOGI(TAG, "User '%s' logged out", active_sessions[i].username);
            memset(&active_sessions[i], 0, sizeof(session_t));
            return;
        }
    }
}

void auth_cleanup_expired_sessions(void) {
    uint32_t current_time = get_current_time();
    int cleaned = 0;
    
    for (int i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (active_sessions[i].valid && current_time > active_sessions[i].expires_at) {
            ESP_LOGI(TAG, "Cleaning up expired session for user '%s'", active_sessions[i].username);
            memset(&active_sessions[i], 0, sizeof(session_t));
            cleaned++;
        }
    }
    
    if (cleaned > 0) {
        ESP_LOGI(TAG, "Cleaned up %d expired sessions", cleaned);
    }
}
