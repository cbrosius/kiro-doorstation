#include "sip_client.h"
#include "audio_handler.h"
#include "dtmf_decoder.h"
#include "rtp_handler.h"
#include "ntp_sync.h"
#include "ntp_log.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/md5.h"
#include "esp_random.h"
#include <inttypes.h>

// Suppress format-truncation warnings for SIP message construction throughout this file
// SIP URIs can be long but our buffers (2048-3072 bytes) are sized appropriately
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

static const char *TAG = "SIP";
static sip_state_t current_state = SIP_STATE_IDLE;
static sip_config_t sip_config = {0};
static int sip_socket = -1;
static TaskHandle_t sip_task_handle = NULL;
static bool registration_requested = false;
static bool reinit_requested = false;
static uint32_t auto_register_delay_ms = 5000; // Wait 5 seconds after init
static uint32_t init_timestamp = 0;
static uint32_t call_start_timestamp = 0;
static uint32_t call_timeout_ms = 30000; // 30 second call timeout
static uint32_t last_message_timestamp = 0; // Track when we last sent a SIP message
static uint32_t sip_response_timeout_ms = 3000; // 3 second timeout for SIP responses
static uint32_t connection_retry_delay_ms = 10000; // 10 seconds before retrying connection
static uint32_t last_connection_retry_timestamp = 0;

// State names for logging (global to avoid stack issues)
static const char* state_names[] = {
    "IDLE", "REGISTERING", "REGISTERED", "CALLING", "RINGING",
    "CONNECTED", "DTMF_SENDING", "DISCONNECTED", "ERROR",
    "AUTH_FAILED", "NETWORK_ERROR", "TIMEOUT"
};

// SIP Log buffer for web interface
#define SIP_LOG_MAX_ENTRIES 50
#define SIP_LOG_MAX_MESSAGE_LEN 256

static sip_log_entry_t sip_log_buffer[SIP_LOG_MAX_ENTRIES];
static int sip_log_write_index = 0;
static int sip_log_count = 0;
static SemaphoreHandle_t sip_log_mutex = NULL;



// SIP authentication challenge structure
typedef struct {
    char realm[128];
    char nonce[128];
    char qop[32];
    char opaque[128];
    char algorithm[32];
    bool valid;
} sip_auth_challenge_t;

static sip_auth_challenge_t last_auth_challenge = {0};

// Store authentication challenge for INVITE messages
static sip_auth_challenge_t invite_auth_challenge = {0};
static bool has_invite_auth_challenge = false;

// Track INVITE authentication attempts to prevent infinite loops
static int invite_auth_attempt_count = 0;
static const int MAX_INVITE_AUTH_ATTEMPTS = 1; // Only retry once for INVITE

// Store INVITE transaction IDs at file scope for branch matching
static int initial_invite_call_id = 0;
static int initial_invite_from_tag = 0;
static int initial_invite_branch = 0;
static int auth_invite_branch = 0;
static int initial_invite_cseq = 1;

// Track authentication state to prevent infinite loops
static int auth_attempt_count = 0;
static const int MAX_AUTH_ATTEMPTS = 3;

// Store Call-ID and From tag from initial REGISTER for reuse in authenticated REGISTER
static char initial_call_id[128] = {0};
static char initial_from_tag[32] = {0};
static bool has_initial_transaction_ids = false;

// Forward declaration
static bool sip_client_register_auth(sip_auth_challenge_t* challenge);

// SIP INVITE template removed - built inline in sip_client_make_call()

// Helper function to add log entry (thread-safe, synchronous with yielding)
// Logs to both serial console and web interface
static void sip_add_log_entry(const char* type, const char* message)
{
    // Log to serial console first
    if (strcmp(type, "error") == 0) {
        NTP_LOGE(TAG, "%s", message);
    } else if (strcmp(type, "info") == 0) {
        NTP_LOGI(TAG, "%s", message);
    } else if (strcmp(type, "sent") == 0 || strcmp(type, "received") == 0) {
        NTP_LOGI(TAG, "[%s] %s", type, message);  // Changed from LOGD to LOGI for visibility
    } else {
        NTP_LOGI(TAG, "[%s] %s", type, message);
    }
    
    // Yield to other tasks after serial logging
    taskYIELD();
    
    // Add to web log buffer
    if (!sip_log_mutex) {
        sip_log_mutex = xSemaphoreCreateMutex();
    }
    
    if (xSemaphoreTake(sip_log_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sip_log_entry_t* entry = &sip_log_buffer[sip_log_write_index];
        
        // Use NTP timestamp if available, otherwise fall back to tick count
        if (ntp_is_synced()) {
            entry->timestamp = ntp_get_timestamp_ms();
        } else {
            entry->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        
        strncpy(entry->type, type, sizeof(entry->type) - 1);
        entry->type[sizeof(entry->type) - 1] = '\0';
        strncpy(entry->message, message, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';
        
        sip_log_write_index = (sip_log_write_index + 1) % SIP_LOG_MAX_ENTRIES;
        if (sip_log_count < SIP_LOG_MAX_ENTRIES) {
            sip_log_count++;
        }
        
        xSemaphoreGive(sip_log_mutex);
    }
    
    // Yield again after web log update
    taskYIELD();
}

// Calculate MD5 hash and convert to hex string
static void calculate_md5_hex(const char* input, char* output) {
    unsigned char hash[16];
    
    // Use mbedtls_md5() instead of deprecated mbedtls_md5_ret()
    mbedtls_md5((const unsigned char*)input, strlen(input), hash);
    
    // Convert to hex string
    for (size_t i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[32] = '\0';
}

// Generate random cnonce for digest authentication
static void generate_cnonce(char* cnonce_out, size_t len) {
    uint32_t random1 = esp_random();
    uint32_t random2 = esp_random();
    snprintf(cnonce_out, len, "%08" PRIx32 "%08" PRIx32, random1, random2);
}

// Helper to extract quoted value from header
static bool extract_quoted_value(const char* header, const char* key, char* dest, size_t dest_size) {
    const char* start = strstr(header, key);
    if (!start) {
        return false;
    }
    start += strlen(key);
    const char* end = strchr(start, '"');
    if (!end) {
        return false;
    }
    size_t len = end - start;
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    strncpy(dest, start, len);
    dest[len] = '\0';
    return true;
}

// Parse WWW-Authenticate header
static sip_auth_challenge_t parse_www_authenticate(const char* buffer) {
    sip_auth_challenge_t challenge = {0};
    
    const char* auth_header = strstr(buffer, "WWW-Authenticate:");
    if (!auth_header) {
        return challenge;
    }
    
    // Extract realm
    extract_quoted_value(auth_header, "realm=\"", challenge.realm, sizeof(challenge.realm));
    
    // Extract nonce
    extract_quoted_value(auth_header, "nonce=\"", challenge.nonce, sizeof(challenge.nonce));
    
    // Extract qop
    extract_quoted_value(auth_header, "qop=\"", challenge.qop, sizeof(challenge.qop));
    
    // Extract opaque (optional)
    extract_quoted_value(auth_header, "opaque=\"", challenge.opaque, sizeof(challenge.opaque));
    
    // Extract algorithm (optional, defaults to MD5)
    const char* algo_start = strstr(auth_header, "algorithm=");
    if (algo_start) {
        algo_start += 10;
        if (*algo_start == '"') {
            algo_start++;
        }
        const char* algo_end = strpbrk(algo_start, "\",\r\n ");
        if (algo_end) {
            size_t algo_len = algo_end - algo_start;
            if (algo_len < sizeof(challenge.algorithm)) {
                strncpy(challenge.algorithm, algo_start, algo_len);
                challenge.algorithm[algo_len] = '\0';
            }
        }
    } else {
        strcpy(challenge.algorithm, "MD5");
    }
    
    challenge.valid = (strlen(challenge.realm) > 0 && strlen(challenge.nonce) > 0);
    
    if (challenge.valid) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Auth challenge parsed: realm=%s, qop=%s, algorithm=%s", 
                 challenge.realm, challenge.qop, challenge.algorithm);
        sip_add_log_entry("info", log_msg);
    }
    
    return challenge;
}

// Calculate digest authentication response
static void calculate_digest_response(
    const char* username,
    const char* password,
    const char* realm,
    const char* nonce,
    const char* method,
    const char* uri,
    const char* qop,
    const char* nc,
    const char* cnonce,
    char* response_out)
{
    char ha1[33];
    char ha2[33];
    // Use static to avoid stack allocation
    // Thread-safe: only one SIP task accesses this function
    static char ha1_input[256];
    static char ha2_input[256];
    static char response_input[512];

    // Simplified logging to avoid format-truncation warnings with long URIs
    sip_add_log_entry("info", "Calculating digest response");

    // Calculate HA1 = MD5(username:realm:password)
    snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
    calculate_md5_hex(ha1_input, ha1);
    // Log HA1 safely
    sip_add_log_entry("info", "HA1 calculated");

    // Calculate HA2 = MD5(method:uri)
    // According to RFC 3261, for digest authentication:
    // For REGISTER: HA2 = MD5("REGISTER:sip:domain")
    // For INVITE: HA2 = MD5("INVITE:sip:user@domain")
    snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, uri);
    calculate_md5_hex(ha2_input, ha2);
    // Log HA2 safely
    sip_add_log_entry("info", "HA2 calculated");

    // Calculate response
    if (qop && strlen(qop) > 0 && strcmp(qop, "auth") == 0) {
        // With qop=auth: MD5(HA1:nonce:nc:cnonce:qop:HA2)
        snprintf(response_input, sizeof(response_input),
                 "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
        // Log response input safely
        sip_add_log_entry("info", "Response input (with qop): calculated");
    } else {
        // Without qop: MD5(HA1:nonce:HA2)
        snprintf(response_input, sizeof(response_input),
                 "%s:%s:%s", ha1, nonce, ha2);
        // Log response input safely
        sip_add_log_entry("info", "Response input (no qop): calculated");
    }

    calculate_md5_hex(response_input, response_out);
    sip_add_log_entry("info", "Digest response calculated");
}

// Helper function to resolve hostname to IP address using getaddrinfo
static bool resolve_hostname(const char* hostname, struct sockaddr_in* addr, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_DGRAM;   // UDP
    hints.ai_protocol = IPPROTO_UDP;
    
    int ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret != 0 || result == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for %s: %d", hostname, ret);
        if (result) {
            freeaddrinfo(result);
        }
        return false;
    }
    
    // Copy the resolved address
    memcpy(addr, result->ai_addr, sizeof(struct sockaddr_in));
    addr->sin_port = htons(port);
    
    freeaddrinfo(result);
    return true;
}

// Helper function to get local IP address
static bool get_local_ip(char* ip_str, size_t max_len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGW(TAG, "WiFi STA interface not found");
        return false;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }
    
    ESP_LOGW(TAG, "Failed to get IP address");
    return false;
}

// SIP task runs on Core 1 (APP CPU) to avoid interfering with WiFi on Core 0
static void sip_task(void *pvParameters __attribute__((unused)))
{
    // Allocate buffer on heap instead of static to avoid memory issues
    const size_t buffer_size = 1536;  // Reduced from 2048 to 1536 bytes
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate SIP receive buffer");
        vTaskDelete(NULL);
        return;
    }
    int len;
    
    sip_add_log_entry("info", "SIP task started on Core 1");
    
    while (1) {
        // Longer delay to minimize CPU usage - SIP doesn't need fast polling
        // 1000ms (1 second) reduces system load significantly
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Yield to WiFi and other high-priority tasks
        taskYIELD();
        
        // Handle reinitialization request (from web interface)
        if (reinit_requested) {
            reinit_requested = false;
            sip_add_log_entry("info", "Processing reinitialization request");
            
            // Close socket if open
            if (sip_socket >= 0) {
                close(sip_socket);
                sip_socket = -1;
                sip_add_log_entry("info", "SIP socket closed for reinit");
            }
            
            // Stop RTP if active
            if (rtp_is_active()) {
                rtp_stop_session();
                sip_add_log_entry("info", "RTP session stopped for reinit");
            }
            
            // Reload configuration from NVS
            sip_config = sip_load_config();
            
            if (sip_config.configured) {
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "Configuration reloaded: %s@%s", 
                         sip_config.username, sip_config.server);
                sip_add_log_entry("info", log_msg);
                
                // Create new socket
                sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sip_socket >= 0) {
                    // Bind socket to port 5060
                    struct sockaddr_in local_addr;
                    memset(&local_addr, 0, sizeof(local_addr));
                    local_addr.sin_family = AF_INET;
                    local_addr.sin_addr.s_addr = INADDR_ANY;
                    local_addr.sin_port = htons(5060);
                    
                    if (bind(sip_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) == 0) {
                        sip_add_log_entry("info", "SIP socket recreated and bound");
                        current_state = SIP_STATE_IDLE;
                        
                        // Trigger auto-registration after delay
                        init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                        sip_add_log_entry("info", "Auto-registration scheduled");
                    } else {
                        sip_add_log_entry("error", "Failed to bind socket after reinit");
                        close(sip_socket);
                        sip_socket = -1;
                        current_state = SIP_STATE_ERROR;
                    }
                } else {
                    sip_add_log_entry("error", "Failed to create socket after reinit");
                    current_state = SIP_STATE_ERROR;
                }
            } else {
                sip_add_log_entry("info", "No configuration found after reinit");
                current_state = SIP_STATE_DISCONNECTED;
            }
        }
        
        // Check for call timeout
        if ((current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) &&
            call_start_timestamp > 0) {
            uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - call_start_timestamp;
            if (elapsed >= call_timeout_ms) {
                sip_add_log_entry("error", "Call timeout - no response from server");
                call_start_timestamp = 0;
                current_state = SIP_STATE_REGISTERED;
                audio_stop_recording();
                audio_stop_playback();
                rtp_stop_session();
            }
        }

        // Check for SIP response timeout (no response to any sent message)
        if (last_message_timestamp > 0 && sip_socket >= 0) {
            uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - last_message_timestamp;
            if (elapsed >= sip_response_timeout_ms) {
                sip_add_log_entry("error", "SIP response timeout - no response from server for 3 seconds");

                // Reset connection and schedule retry
                if (sip_socket >= 0) {
                    close(sip_socket);
                    sip_socket = -1;
                    sip_add_log_entry("info", "SIP socket closed due to timeout");
                }

                // Clear any pending states
                if (current_state == SIP_STATE_REGISTERING) {
                    current_state = SIP_STATE_DISCONNECTED;
                    auth_attempt_count = 0;
                    has_initial_transaction_ids = false;
                } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                    current_state = SIP_STATE_REGISTERED;
                    call_start_timestamp = 0;
                    audio_stop_recording();
                    audio_stop_playback();
                    rtp_stop_session();
                } else if (current_state == SIP_STATE_REGISTERED) {
                    // If we were registered, go to disconnected state
                    current_state = SIP_STATE_DISCONNECTED;
                }

                // Clear authentication challenges
                has_invite_auth_challenge = false;
                memset(&invite_auth_challenge, 0, sizeof(invite_auth_challenge));
                invite_auth_attempt_count = 0;

                last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                last_message_timestamp = 0;
                sip_add_log_entry("info", "Connection retry scheduled in 10 seconds");
            }
        }

        // Check if it's time to retry connection after timeout
        if (last_connection_retry_timestamp > 0) {
            uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - last_connection_retry_timestamp;
            if (elapsed >= connection_retry_delay_ms) {
                last_connection_retry_timestamp = 0;
                
                char retry_debug[256];
                snprintf(retry_debug, sizeof(retry_debug),
                         "Retrying SIP connection: current_state=%s, socket=%d, configured=%d",
                         (current_state < sizeof(state_names)/sizeof(state_names[0])) ?
                         state_names[current_state] : "UNKNOWN",
                         sip_socket, sip_config.configured ? 1 : 0);
                sip_add_log_entry("info", retry_debug);

                // Recreate socket and try to register again
                if (sip_socket < 0) {
                    sip_add_log_entry("info", "Creating new socket for retry");
                    sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    if (sip_socket >= 0) {
                        struct sockaddr_in local_addr;
                        memset(&local_addr, 0, sizeof(local_addr));
                        local_addr.sin_family = AF_INET;
                        local_addr.sin_addr.s_addr = INADDR_ANY;
                        local_addr.sin_port = htons(5060);

                        if (bind(sip_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) == 0) {
                            char bind_msg[128];
                            snprintf(bind_msg, sizeof(bind_msg),
                                     "Socket recreated (fd=%d), changing state to IDLE", sip_socket);
                            sip_add_log_entry("info", bind_msg);
                            current_state = SIP_STATE_IDLE;

                            // Trigger auto-registration
                            init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                            sip_add_log_entry("info", "Auto-registration timestamp set - will trigger in next loop iteration");
                        } else {
                            sip_add_log_entry("error", "Failed to bind socket after timeout - will retry later");
                            close(sip_socket);
                            sip_socket = -1;
                            // Reschedule retry
                            last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                        }
                    } else {
                        sip_add_log_entry("error", "Failed to create socket after timeout - will retry later");
                        // Reschedule retry
                        last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    }
                } else {
                    char socket_msg[128];
                    snprintf(socket_msg, sizeof(socket_msg),
                             "Socket already exists (fd=%d) during retry - closing and recreating", sip_socket);
                    sip_add_log_entry("info", socket_msg);
                    close(sip_socket);
                    sip_socket = -1;
                    // Let next iteration handle recreation
                    last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                }
            }
        }
        
        // Auto-registration after delay (if configured and not already registered)
        if (init_timestamp > 0 &&
            current_state == SIP_STATE_IDLE &&
            sip_config.configured &&
            (xTaskGetTickCount() * portTICK_PERIOD_MS - init_timestamp) >= auto_register_delay_ms) {
            
            char auto_reg_msg[256];
            snprintf(auto_reg_msg, sizeof(auto_reg_msg),
                     "Auto-registration triggered: state=%s, socket=%d, configured=%d",
                     state_names[current_state], sip_socket, sip_config.configured ? 1 : 0);
            sip_add_log_entry("info", auto_reg_msg);
            
            init_timestamp = 0; // Clear flag so we only try once
            registration_requested = true;
            sip_add_log_entry("info", "registration_requested flag set to true");
        }
        
        // Check if registration was requested (manual or auto)
        if (registration_requested && current_state != SIP_STATE_REGISTERED) {
            char reg_debug[256];
            snprintf(reg_debug, sizeof(reg_debug),
                     "Processing registration: state=%s, socket=%d",
                     (current_state < sizeof(state_names)/sizeof(state_names[0])) ?
                     state_names[current_state] : "UNKNOWN", sip_socket);
            sip_add_log_entry("info", reg_debug);
            
            registration_requested = false;
            
            // Recreate socket if it was closed
            if (sip_socket < 0) {
                sip_add_log_entry("info", "Socket closed - recreating before registration");
                
                sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sip_socket < 0) {
                    ESP_LOGE(TAG, "Error creating SIP socket");
                    sip_add_log_entry("error", "Failed to create SIP socket");
                    current_state = SIP_STATE_ERROR;
                    sip_add_log_entry("error", "State changed to ERROR");
                    continue;
                }
                
                // Bind socket to port 5060
                struct sockaddr_in local_addr;
                memset(&local_addr, 0, sizeof(local_addr));
                local_addr.sin_family = AF_INET;
                local_addr.sin_addr.s_addr = INADDR_ANY;
                local_addr.sin_port = htons(5060);
                
                if (bind(sip_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
                    ESP_LOGE(TAG, "Error binding SIP socket to port 5060");
                    sip_add_log_entry("error", "Failed to bind socket to port 5060");
                    close(sip_socket);
                    sip_socket = -1;
                    current_state = SIP_STATE_ERROR;
                    sip_add_log_entry("error", "State changed to ERROR");
                    continue;
                }
                
                sip_add_log_entry("info", "Socket recreated and bound - ready for registration");
            } else {
                char socket_ok_msg[128];
                snprintf(socket_ok_msg, sizeof(socket_ok_msg),
                         "Socket already exists (fd=%d) - proceeding with registration", sip_socket);
                sip_add_log_entry("info", socket_ok_msg);
            }
            
            sip_add_log_entry("info", "Calling sip_client_register()");
            sip_client_register();
            sip_add_log_entry("info", "sip_client_register() completed");
        }
        
        if (sip_socket >= 0) {
            len = recv(sip_socket, buffer, buffer_size - 1, MSG_DONTWAIT);
            if (len > 0) {
                buffer[len] = '\0';

                // Reset timeout timestamp when we receive any response
                last_message_timestamp = 0;

                sip_add_log_entry("received", "SIP message received");

                // Log received message (truncated for display)
                char log_msg[SIP_LOG_MAX_MESSAGE_LEN];
                snprintf(log_msg, sizeof(log_msg), "%.200s%s", buffer, len > 200 ? "..." : "");
                sip_add_log_entry("received", log_msg);
                
                // Enhanced SIP message processing with better error handling
                // Check response codes first (more specific than method names)
                if (strstr(buffer, "SIP/2.0 200 OK")) {
                    if (current_state == SIP_STATE_REGISTERING) {
                        current_state = SIP_STATE_REGISTERED;
                        auth_attempt_count = 0;  // Reset counter on success
                        has_initial_transaction_ids = false;  // Clear stored IDs
                        sip_add_log_entry("info", "SIP registration successful");
                    } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        // Clear stored INVITE auth challenge on successful call
                        has_invite_auth_challenge = false;
                        memset(&invite_auth_challenge, 0, sizeof(invite_auth_challenge));
                        invite_auth_attempt_count = 0;  // Reset counter
                        
                        char success_log[128];
                        snprintf(success_log, sizeof(success_log),
                                 "INVITE authentication successful after %d attempt(s)",
                                 invite_auth_attempt_count + 1);
                        sip_add_log_entry("info", success_log);

                        // Call accepted - send ACK and start audio
                        sip_add_log_entry("info", "Call accepted (200 OK)");

                        // Extract To tag for ACK
                        char to_tag[32] = {0};
                        const char* to_hdr = strstr(buffer, "To:");
                        if (to_hdr) {
                            const char* tag_ptr = strstr(to_hdr, "tag=");
                            if (tag_ptr) {
                                tag_ptr += 4;
                                const char* tag_term = strpbrk(tag_ptr, ";\r\n ");
                                if (tag_term) {
                                    size_t tag_length = tag_term - tag_ptr;
                                    if (tag_length < sizeof(to_tag)) {
                                        strncpy(to_tag, tag_ptr, tag_length);
                                        to_tag[tag_length] = '\0';
                                    }
                                }
                            }
                        }
                        
                        // Send ACK to complete call setup
                        static char ack_msg[768];  // Increased buffer size
                        char local_ip[16];
                        if (!get_local_ip(local_ip, sizeof(local_ip))) {
                            strcpy(local_ip, "192.168.1.100");
                        }
                        
                        // Extract Call-ID and CSeq for ACK
                        char call_id[128] = {0};  // Increased buffer size
                        const char* cid_start = strstr(buffer, "Call-ID:");
                        if (cid_start) {
                            cid_start += 8;
                            while (*cid_start == ' ') {
                                cid_start++;
                            }
                            const char* cid_end = strstr(cid_start, "\r\n");
                            if (cid_end) {
                                size_t cid_len = cid_end - cid_start;
                                if (cid_len < sizeof(call_id) - 1) {
                                    strncpy(call_id, cid_start, cid_len);
                                    call_id[cid_len] = '\0';
                                }
                            }
                        }
                        
                        // Build ACK message
                        snprintf(ack_msg, sizeof(ack_msg),
                                "ACK sip:%s@%s SIP/2.0\r\n"
                                "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d\r\n"
                                "From: <sip:%s@%s>;tag=%d\r\n"
                                "To: <sip:%s@%s>;tag=%s\r\n"
                                "Call-ID: %s\r\n"
                                "CSeq: 1 ACK\r\n"
                                "Max-Forwards: 70\r\n"
                                "Content-Length: 0\r\n\r\n",
                                sip_config.username, sip_config.server,
                                local_ip, rand(),
                                sip_config.username, sip_config.server, rand(),
                                sip_config.username, sip_config.server, to_tag,
                                call_id);
                        
                        // Send ACK
                        struct sockaddr_in server_addr;
                        if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
                            sendto(sip_socket, ack_msg, strlen(ack_msg), 0,
                                  (struct sockaddr*)&server_addr, sizeof(server_addr));
                            sip_add_log_entry("sent", "ACK sent");
                        }
                        
                        // Extract remote RTP port from SDP (simplified - assumes port 5004)
                        // In a full implementation, parse the SDP m= line
                        uint16_t remote_rtp_port = 5004;
                        const char* sdp_start = strstr(buffer, "\r\n\r\n");
                        if (sdp_start) {
                            const char* m_line = strstr(sdp_start, "m=audio ");
                            if (m_line) {
                                m_line += 8;
                                int port_val = atoi(m_line);
                                if (port_val > 0 && port_val <= 65535) {
                                    remote_rtp_port = (uint16_t)port_val;
                                }
                            }
                        }
                        
                        // Get remote IP from SIP server (use first 15 chars to fit in buffer)
                        char remote_ip[64];  // Increased buffer size to match server field
                        strncpy(remote_ip, sip_config.server, sizeof(remote_ip) - 1);
                        remote_ip[sizeof(remote_ip) - 1] = '\0';
                        
                        // Start RTP session
                        if (rtp_start_session(remote_ip, remote_rtp_port, 5004)) {
                            sip_add_log_entry("info", "RTP session started");
                        } else {
                            sip_add_log_entry("error", "Failed to start RTP session");
                        }
                        
                        // Start audio
                        current_state = SIP_STATE_CONNECTED;
                        call_start_timestamp = 0; // Clear timeout
                        sip_add_log_entry("info", "Call connected - State: CONNECTED");
                        
                        // Reset DTMF decoder state for new call
                        dtmf_reset_call_state();
                        
                        audio_start_recording();
                        audio_start_playback();
                    }
                } else if (strstr(buffer, "SIP/2.0 180 Ringing")) {
                    if (current_state == SIP_STATE_CALLING) {
                        current_state = SIP_STATE_RINGING;
                        sip_add_log_entry("info", "Call ringing (180 Ringing)");
                    }
                } else if (strstr(buffer, "SIP/2.0 183 Session Progress")) {
                    if (current_state == SIP_STATE_CALLING) {
                        sip_add_log_entry("info", "Session progress (183)");
                    }
                } else if (strstr(buffer, "SIP/2.0 401 Unauthorized")) {
                    if (current_state == SIP_STATE_REGISTERING) {
                        auth_attempt_count++;

                        char log_msg[128];
                        snprintf(log_msg, sizeof(log_msg), "Authentication required (attempt %d/%d), parsing challenge",
                                 auth_attempt_count, MAX_AUTH_ATTEMPTS);
                        sip_add_log_entry("info", log_msg);

                        // Check if we've exceeded max attempts (prevent infinite loop)
                        if (auth_attempt_count > MAX_AUTH_ATTEMPTS) {
                            sip_add_log_entry("error", "Max authentication attempts exceeded - authentication failed");
                            current_state = SIP_STATE_AUTH_FAILED;
                            auth_attempt_count = 0;
                            has_initial_transaction_ids = false;
                            // Don't return - let the task continue to handle other operations
                            // The task should not return, as per FreeRTOS requirements
                            continue;
                        }

                        // Parse authentication challenge
                        last_auth_challenge = parse_www_authenticate(buffer);

                        if (last_auth_challenge.valid) {
                            // Send authenticated REGISTER
                            sip_client_register_auth(&last_auth_challenge);
                        } else {
                            sip_add_log_entry("error", "Failed to parse auth challenge");
                            current_state = SIP_STATE_AUTH_FAILED;
                            auth_attempt_count = 0;
                            has_initial_transaction_ids = false;
                            sip_add_log_entry("error", "State changed to AUTH_FAILED");
                        }
                    } else if (current_state == SIP_STATE_CALLING) {
                        // CRITICAL: Check if this 401 is for our authenticated INVITE or a retransmission
                        // initial_invite_branch = first INVITE without auth
                        // auth_invite_branch = authenticated INVITE (only set when retry happens)
                        
                        const char* via_ptr = strstr(buffer, "Via:");
                        bool is_retransmission = false;
                        
                        if (via_ptr && has_invite_auth_challenge && auth_invite_branch != 0) {
                            // We've sent an authenticated INVITE - check if this 401 is for it
                            const char* branch_param = strstr(via_ptr, "branch=z9hG4bK");
                            if (branch_param) {
                                branch_param += 14; // Skip "branch=z9hG4bK"
                                // Extract just the numeric part
                                int received_branch = atoi(branch_param);
                                
                                char branch_log[256];
                                snprintf(branch_log, sizeof(branch_log),
                                         "401 response branch=%d, initial=%d, auth=%d",
                                         received_branch, initial_invite_branch, auth_invite_branch);
                                sip_add_log_entry("info", branch_log);
                                
                                // If this 401 is for the initial INVITE (before auth), it's a retransmission
                                if (received_branch == initial_invite_branch && received_branch != auth_invite_branch) {
                                    is_retransmission = true;
                                    sip_add_log_entry("info", "401 is retransmission of initial challenge - ignoring");
                                }
                            }
                        }
                        
                        // If this is a retransmission, ignore it
                        if (is_retransmission) {
                            continue;
                        }
                        
                        // Check if we've exceeded max attempts
                        if (invite_auth_attempt_count >= MAX_INVITE_AUTH_ATTEMPTS) {
                            char err_msg[256];
                            snprintf(err_msg, sizeof(err_msg),
                                     "Max INVITE auth attempts (%d) exceeded - giving up on this call",
                                     MAX_INVITE_AUTH_ATTEMPTS);
                            sip_add_log_entry("error", err_msg);
                            current_state = SIP_STATE_REGISTERED;
                            call_start_timestamp = 0;
                            has_invite_auth_challenge = false;
                            invite_auth_attempt_count = 0;
                            
                            // Log the authentication failure details for debugging
                            char fail_debug[512];
                            snprintf(fail_debug, sizeof(fail_debug),
                                     "INVITE auth failed - Last attempt used: nonce=%s, calculated response=%s",
                                     invite_auth_challenge.nonce, "see previous log");
                            sip_add_log_entry("error", fail_debug);
                            continue;
                        }
                        
                        // Parse the new authentication challenge
                        sip_auth_challenge_t new_challenge = parse_www_authenticate(buffer);
                        if (new_challenge.valid) {
                            // Update the stored challenge with the latest one from server
                            // Only reset counter if this is a genuinely NEW nonce (different from previous)
                            if (!has_invite_auth_challenge ||
                                strcmp(invite_auth_challenge.nonce, new_challenge.nonce) != 0) {
                                char log_msg[256];
                                snprintf(log_msg, sizeof(log_msg),
                                         "New INVITE auth challenge (nonce changed) - resetting attempt counter");
                                sip_add_log_entry("info", log_msg);
                                invite_auth_attempt_count = 0;
                            }
                            
                            invite_auth_challenge = new_challenge;
                            has_invite_auth_challenge = true;
                            sip_add_log_entry("info", "INVITE authentication challenge updated - will retry with auth");

                            // Extract the target URI from the To header in the 401 response
                            static char retry_uri[128] = {0};
                            const char* to_header = strstr(buffer, "To: ");
                            if (to_header) {
                                to_header += 4; // Skip "To: "
                                const char* uri_start = strstr(to_header, "<sip:");
                                if (uri_start) {
                                    uri_start += 1; // Skip "<"
                                    const char* uri_end = strstr(uri_start, ">");
                                    if (uri_end) {
                                        size_t uri_len = uri_end - uri_start;
                                        if (uri_len < sizeof(retry_uri)) {
                                            strncpy(retry_uri, uri_start, uri_len);
                                            retry_uri[uri_len] = '\0';
                                            
                                            char retry_log[256];
                                            snprintf(retry_log, sizeof(retry_log),
                                                     "Retrying INVITE with auth (attempt %d/%d) to %s",
                                                     invite_auth_attempt_count + 1, MAX_INVITE_AUTH_ATTEMPTS, retry_uri);
                                            sip_add_log_entry("info", retry_log);
                                            sip_client_make_call(retry_uri);
                                        }
                                    }
                                }
                            }
                        } else {
                            sip_add_log_entry("error", "Failed to parse INVITE auth challenge");
                            current_state = SIP_STATE_REGISTERED;
                            call_start_timestamp = 0;
                            has_invite_auth_challenge = false;
                            invite_auth_attempt_count = 0;
                        }
                    } else {
                        // Received 401 but not in REGISTERING or CALLING state
                        // This is likely a retransmitted 401 from server - ignore it
                        char ignore_msg[128];
                        snprintf(ignore_msg, sizeof(ignore_msg),
                                 "Ignoring 401 in state %s (likely retransmission)",
                                 (current_state < sizeof(state_names)/sizeof(state_names[0])) ?
                                 state_names[current_state] : "UNKNOWN");
                        sip_add_log_entry("info", ignore_msg);
                    }
                } else if (strstr(buffer, "SIP/2.0 100 Trying")) {
                    // Provisional response, just log it
                    sip_add_log_entry("info", "Server processing request (100 Trying)");
                } else if (strstr(buffer, "SIP/2.0 403 Forbidden")) {
                    sip_add_log_entry("error", "SIP forbidden - State: AUTH_FAILED");
                    if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        call_start_timestamp = 0; // Clear timeout
                        current_state = SIP_STATE_REGISTERED; // Return to registered state
                    } else {
                        current_state = SIP_STATE_AUTH_FAILED;
                    }
                } else if (strstr(buffer, "SIP/2.0 404 Not Found")) {
                    sip_add_log_entry("error", "SIP target not found");
                    if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        call_start_timestamp = 0; // Clear timeout
                        current_state = SIP_STATE_REGISTERED; // Return to registered state
                    } else {
                        current_state = SIP_STATE_ERROR;
                    }
                } else if (strstr(buffer, "SIP/2.0 408 Request Timeout")) {
                    sip_add_log_entry("error", "SIP request timeout");
                    if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        call_start_timestamp = 0; // Clear timeout
                        current_state = SIP_STATE_REGISTERED; // Return to registered state
                    } else {
                        current_state = SIP_STATE_TIMEOUT;
                    }
                } else if (strstr(buffer, "SIP/2.0 486 Busy Here")) {
                    sip_add_log_entry("info", "SIP target busy");
                    call_start_timestamp = 0; // Clear timeout
                    current_state = SIP_STATE_REGISTERED;
                } else if (strstr(buffer, "SIP/2.0 487 Request Terminated")) {
                    sip_add_log_entry("info", "SIP request terminated");
                    call_start_timestamp = 0; // Clear timeout
                    current_state = SIP_STATE_REGISTERED;
                } else if (strstr(buffer, "SIP/2.0 500 Internal Server Error")) {
                    // Handle 500 Internal Server Error from SIP server
                    sip_add_log_entry("error", "SIP 500 Internal Server Error received");
                    
                    char debug_msg[256];
                    snprintf(debug_msg, sizeof(debug_msg),
                             "500 Error - Current state: %s, Socket: %d, Auth attempts: %d",
                             (current_state < sizeof(state_names)/sizeof(state_names[0])) ?
                             state_names[current_state] : "UNKNOWN",
                             sip_socket, auth_attempt_count);
                    sip_add_log_entry("error", debug_msg);
                    
                    // Clear any pending states and authentication
                    if (current_state == SIP_STATE_REGISTERING) {
                        sip_add_log_entry("error", "500 during REGISTER - clearing auth state");
                        auth_attempt_count = 0;
                        has_initial_transaction_ids = false;
                        current_state = SIP_STATE_DISCONNECTED;
                    } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        sip_add_log_entry("error", "500 during call setup - returning to registered");
                        call_start_timestamp = 0;
                        has_invite_auth_challenge = false;
                        invite_auth_attempt_count = 0;
                        current_state = SIP_STATE_REGISTERED;
                        audio_stop_recording();
                        audio_stop_playback();
                        rtp_stop_session();
                    } else {
                        sip_add_log_entry("error", "500 in other state - entering error state");
                        current_state = SIP_STATE_ERROR;
                    }
                    
                    // Close socket so retry mechanism can recreate it
                    if (sip_socket >= 0) {
                        close(sip_socket);
                        sip_socket = -1;
                        sip_add_log_entry("info", "SIP socket closed after 500 error");
                    }
                    
                    // Schedule connection retry
                    last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    sip_add_log_entry("info", "Connection retry scheduled in 10 seconds after 500 error");
                } else if (strstr(buffer, "SIP/2.0 503 Service Unavailable")) {
                    // Handle 503 Service Unavailable (server overloaded/down)
                    sip_add_log_entry("error", "SIP 503 Service Unavailable");
                    
                    if (current_state == SIP_STATE_REGISTERING) {
                        auth_attempt_count = 0;
                        has_initial_transaction_ids = false;
                        current_state = SIP_STATE_DISCONNECTED;
                    } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
                        call_start_timestamp = 0;
                        current_state = SIP_STATE_REGISTERED;
                        audio_stop_recording();
                        audio_stop_playback();
                        rtp_stop_session();
                    }
                    
                    // Close socket so retry mechanism can recreate it
                    if (sip_socket >= 0) {
                        close(sip_socket);
                        sip_socket = -1;
                        sip_add_log_entry("info", "SIP socket closed after 503 error");
                    }
                    
                    last_connection_retry_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    sip_add_log_entry("info", "Connection retry scheduled in 10 seconds after 503 error");
                } else if (strstr(buffer, "SIP/2.0 603 Decline")) {
                    sip_add_log_entry("info", "Call declined by remote party");
                    call_start_timestamp = 0; // Clear timeout
                    current_state = SIP_STATE_REGISTERED;
                } else if (strncmp(buffer, "INVITE ", 7) == 0) {
                    // Check for INVITE request (not response)
                    sip_add_log_entry("info", "Incoming INVITE detected");
                    
                    // Only accept if we're in IDLE or REGISTERED state
                    if (current_state != SIP_STATE_IDLE && current_state != SIP_STATE_REGISTERED) {
                        const char* current_state_name = (current_state < sizeof(state_names)/sizeof(state_names[0])) ? 
                                                state_names[current_state] : "UNKNOWN";
                        char busy_msg[128];
                        snprintf(busy_msg, sizeof(busy_msg), "Busy - cannot accept call (state: %s)", current_state_name);
                        sip_add_log_entry("error", busy_msg);
                        // Send 486 Busy Here response would be implemented here
                        continue;
                    }
                    
                    sip_add_log_entry("info", "Processing incoming call");
                    
                    // Extract headers for response
                    static char call_id[128] = {0};
                    static char from_header[256] = {0};
                    static char to_header[256] = {0};
                    static char via_header[256] = {0};
                    int cseq_num = 1;
                    
                    // Extract Call-ID
                    const char* cid_ptr = strstr(buffer, "Call-ID:");
                    if (cid_ptr) {
                        cid_ptr += 8;
                        while (*cid_ptr == ' ') {
                            cid_ptr++;
                        }
                        const char* cid_term = strstr(cid_ptr, "\r\n");
                        if (cid_term) {
                            size_t cid_length = cid_term - cid_ptr;
                            if (cid_length < sizeof(call_id) - 1) {
                                strncpy(call_id, cid_ptr, cid_length);
                                call_id[cid_length] = '\0';
                            }
                        }
                    }
                    
                    // Extract From
                    const char* from_ptr = strstr(buffer, "From:");
                    if (from_ptr) {
                        from_ptr += 5;
                        while (*from_ptr == ' ') {
                            from_ptr++;
                        }
                        const char* from_term = strstr(from_ptr, "\r\n");
                        if (from_term) {
                            size_t from_length = from_term - from_ptr;
                            if (from_length < sizeof(from_header) - 1) {
                                strncpy(from_header, from_ptr, from_length);
                                from_header[from_length] = '\0';
                            }
                        }
                    }
                    
                    // Extract To
                    const char* to_ptr = strstr(buffer, "To:");
                    if (to_ptr) {
                        to_ptr += 3;
                        while (*to_ptr == ' ') {
                            to_ptr++;
                        }
                        const char* to_term = strstr(to_ptr, "\r\n");
                        if (to_term) {
                            size_t to_length = to_term - to_ptr;
                            if (to_length < sizeof(to_header) - 1) {
                                strncpy(to_header, to_ptr, to_length);
                                to_header[to_length] = '\0';
                            }
                        }
                    }
                    
                    // Extract Via
                    const char* via_ptr = strstr(buffer, "Via:");
                    if (via_ptr) {
                        via_ptr += 4;
                        while (*via_ptr == ' ') {
                            via_ptr++;
                        }
                        const char* via_term = strstr(via_ptr, "\r\n");
                        if (via_term) {
                            size_t via_length = via_term - via_ptr;
                            if (via_length < sizeof(via_header) - 1) {
                                strncpy(via_header, via_ptr, via_length);
                                via_header[via_length] = '\0';
                            }
                        }
                    }
                    
                    // Extract CSeq
                    const char* cseq_ptr = strstr(buffer, "CSeq:");
                    if (cseq_ptr) {
                        cseq_ptr += 5;
                        while (*cseq_ptr == ' ') {
                            cseq_ptr++;
                        }
                        cseq_num = atoi(cseq_ptr);
                    }
                    
                    // Get local IP
                    char local_ip[16];
                    if (!get_local_ip(local_ip, sizeof(local_ip))) {
                        strcpy(local_ip, "192.168.1.100");
                    }
                    
                    // Create SDP for response
                    static char sdp[256];
                    snprintf(sdp, sizeof(sdp), 
                             "v=0\r\n"
                             "o=- %d 0 IN IP4 %s\r\n"
                             "s=ESP32 Doorbell\r\n"
                             "c=IN IP4 %s\r\n"
                             "t=0 0\r\n"
                             "m=audio 5004 RTP/AVP 0 8 101\r\n"
                             "a=rtpmap:0 PCMU/8000\r\n"
                             "a=rtpmap:8 PCMA/8000\r\n"
                             "a=rtpmap:101 telephone-event/8000\r\n"
                             "a=fmtp:101 0-15\r\n"
                             "a=sendrecv\r\n",
                             rand(), local_ip, local_ip);
                    
                    // Add tag to To header if not present
                    char to_with_tag[300];
                    if (strstr(to_header, "tag=") == NULL) {
                        snprintf(to_with_tag, sizeof(to_with_tag), "%s;tag=%d", to_header, rand());
                    } else {
                        strncpy(to_with_tag, to_header, sizeof(to_with_tag) - 1);
                        to_with_tag[sizeof(to_with_tag) - 1] = '\0';
                    }
                    
                    // Build 200 OK response
                    static char response[1536];
                    snprintf(response, sizeof(response),
                             "SIP/2.0 200 OK\r\n"
                             "Via: %s\r\n"
                             "From: %s\r\n"
                             "To: %s\r\n"
                             "Call-ID: %s\r\n"
                             "CSeq: %d INVITE\r\n"
                             "Contact: <sip:%s@%s:5060>\r\n"
                             "Content-Type: application/sdp\r\n"
                             "Content-Length: %d\r\n\r\n%s",
                             via_header,
                             from_header,
                             to_with_tag,
                             call_id,
                             cseq_num,
                             sip_config.username, local_ip,
                             strlen(sdp), sdp);
                    
                    // Send 200 OK
                    struct sockaddr_in server_addr;
                    
                    if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
                        int sent = sendto(sip_socket, response, strlen(response), 0,
                                         (struct sockaddr*)&server_addr, sizeof(server_addr));
                        if (sent > 0) {
                            sip_add_log_entry("sent", "200 OK response to INVITE");
                            
                            // Start RTP session
                            char remote_ip[64];
                            strncpy(remote_ip, sip_config.server, sizeof(remote_ip) - 1);
                            remote_ip[sizeof(remote_ip) - 1] = '\0';
                            
                            if (rtp_start_session(remote_ip, 5004, 5004)) {
                                sip_add_log_entry("info", "RTP session started");
                            }
                            
                            // Update state
                            current_state = SIP_STATE_CONNECTED;
                            call_start_timestamp = 0;
                            sip_add_log_entry("info", "Incoming call answered - State: CONNECTED");
                            
                            // Reset DTMF decoder state for new call
                            dtmf_reset_call_state();
                            
                            audio_start_recording();
                            audio_start_playback();
                        } else {
                            sip_add_log_entry("error", "Failed to send 200 OK");
                        }
                    } else {
                        sip_add_log_entry("error", "DNS lookup failed");
                    }
                } else if (strncmp(buffer, "BYE sip:", 8) == 0 || strncmp(buffer, "BYE ", 4) == 0) {
                    // Check for BYE request (not response)
                    sip_add_log_entry("info", "Call ended by remote party");
                    
                    // Send 200 OK response to BYE
                    static char bye_response[768];  // Increased buffer size
                    char local_ip[16];
                    if (!get_local_ip(local_ip, sizeof(local_ip))) {
                        strcpy(local_ip, "192.168.1.100");
                    }
                    
                    // Extract Call-ID and CSeq for response
                    char call_id[128] = {0};  // Increased buffer size
                    int cseq_num = 1;
                    const char* bye_cid_ptr = strstr(buffer, "Call-ID:");
                    if (bye_cid_ptr) {
                        bye_cid_ptr += 8;
                        while (*bye_cid_ptr == ' ') {
                            bye_cid_ptr++;
                        }
                        const char* bye_cid_term = strstr(bye_cid_ptr, "\r\n");
                        if (bye_cid_term) {
                            size_t bye_cid_len = bye_cid_term - bye_cid_ptr;
                            if (bye_cid_len < sizeof(call_id)) {
                                strncpy(call_id, bye_cid_ptr, bye_cid_len);
                                call_id[bye_cid_len] = '\0';
                            }
                        }
                    }
                    
                    const char* bye_cseq_ptr = strstr(buffer, "CSeq:");
                    if (bye_cseq_ptr) {
                        bye_cseq_ptr += 5;
                        while (*bye_cseq_ptr == ' ') {
                            bye_cseq_ptr++;
                        }
                        cseq_num = atoi(bye_cseq_ptr);
                    }
                    
                    snprintf(bye_response, sizeof(bye_response),
                            "SIP/2.0 200 OK\r\n"
                            "Via: SIP/2.0/UDP %s:5060\r\n"
                            "From: <sip:%s@%s>\r\n"
                            "To: <sip:%s@%s>\r\n"
                            "Call-ID: %s\r\n"
                            "CSeq: %d BYE\r\n"
                            "Content-Length: 0\r\n\r\n",
                            local_ip,
                            sip_config.username, sip_config.server,
                            sip_config.username, sip_config.server,
                            call_id, cseq_num);
                    
                    struct sockaddr_in server_addr;
                    if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
                        sendto(sip_socket, bye_response, strlen(bye_response), 0,
                              (struct sockaddr*)&server_addr, sizeof(server_addr));
                        sip_add_log_entry("sent", "200 OK response to BYE");
                    }
                    
                    current_state = SIP_STATE_REGISTERED;
                    call_start_timestamp = 0; // Clear timeout
                    
                    // Reset DTMF decoder state when call ends
                    dtmf_reset_call_state();
                    
                    audio_stop_recording();
                    audio_stop_playback();
                    rtp_stop_session();
                    sip_add_log_entry("info", "RTP session stopped");
                }
            }
        }
        
        // Audio processing during active call
        if (current_state == SIP_STATE_CONNECTED && rtp_is_active()) {
            // Send audio (20ms frames at 8kHz = 160 samples)
            int16_t tx_buffer[160];
            size_t samples_read = audio_read(tx_buffer, 160);
            if (samples_read > 0) {
                // Send audio via RTP
                rtp_send_audio(tx_buffer, samples_read);
            }
            
            // Receive audio
            int16_t rx_buffer[160];
            int samples_received = rtp_receive_audio(rx_buffer, 160);
            if (samples_received > 0) {
                // Play received audio
                audio_write(rx_buffer, samples_received);
            }
        }
    }
    
    ESP_LOGI(TAG, "SIP task ended");
    free(buffer);
    vTaskDelete(NULL);
}

void sip_client_init(void)
{
    sip_add_log_entry("info", "Initializing SIP client");

    // Initialize RTP handler
    rtp_init();

    // Set initial state
    current_state = SIP_STATE_IDLE;

    // Load configuration
    sip_config = sip_load_config();

    if (sip_config.configured) {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "SIP configuration loaded: %s@%s",
                  sip_config.username, sip_config.server);
        sip_add_log_entry("info", log_msg);

        // Check IP status before creating socket
        char local_ip[16];
        if (get_local_ip(local_ip, sizeof(local_ip))) {
            ESP_LOGI(TAG, "SIP init: IP available: %s", local_ip);
        } else {
            ESP_LOGI(TAG, "SIP init: No IP available yet");
        }

        // Create socket
        sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sip_socket < 0) {
            ESP_LOGE(TAG, "Error creating SIP socket");
            current_state = SIP_STATE_ERROR;
            return;
        }
        
        // Bind socket to port 5060 so we can receive responses
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
        local_addr.sin_port = htons(5060);  // SIP port
        
        if (bind(sip_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            ESP_LOGE(TAG, "Error binding SIP socket to port 5060");
            close(sip_socket);
            sip_socket = -1;
            current_state = SIP_STATE_ERROR;
            return;
        }
        
        sip_add_log_entry("info", "SIP socket bound to port 5060");

        // Create SIP task pinned to Core 1 (APP CPU)
        // This isolates SIP from WiFi which runs on Core 0 (PRO CPU)
        // Priority 3 is low enough to not interfere with system tasks
        // Stack size 8KB to handle DNS resolution and SIP messages
        BaseType_t result = xTaskCreatePinnedToCore(
            sip_task,           // Task function
            "sip_task",         // Task name
            8192,               // Stack size (8KB - increased for DNS)
            NULL,               // Parameters
            3,                  // Priority (low)
            &sip_task_handle,   // Task handle
            1                   // Core 1 (APP CPU)
        );
        
        if (result != pdPASS) {
            ESP_LOGE(TAG, "Failed to create SIP task");
            close(sip_socket);
            sip_socket = -1;
            current_state = SIP_STATE_ERROR;
            return;
        }
        
        ESP_LOGI(TAG, "SIP task created on Core 1 (APP CPU)");
        
        // Set timestamp for delayed auto-registration
        // This gives WiFi time to stabilize before attempting registration
        init_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "SIP client ready. Auto-registration will start in %lu ms", auto_register_delay_ms);
        sip_add_log_entry("info", "SIP client ready. Auto-registration scheduled.");
    } else {
        ESP_LOGI(TAG, "No SIP configuration found");
        sip_add_log_entry("info", "No SIP configuration found");
        current_state = SIP_STATE_DISCONNECTED;
    }

    ESP_LOGI(TAG, "SIP client initialized");
}

void sip_client_deinit(void)
{
    if (sip_task_handle) {
        vTaskDelete(sip_task_handle);
        sip_task_handle = NULL;
    }
    
    if (sip_socket >= 0) {
        close(sip_socket);
        sip_socket = -1;
    }
    
    ESP_LOGI(TAG, "SIP Client deinitialized");
}

bool sip_client_register(void)
{
    if (!sip_config.configured || sip_socket < 0) {
        current_state = SIP_STATE_ERROR;
        return false;
    }

    sip_add_log_entry("info", "Starting SIP registration");
    current_state = SIP_STATE_REGISTERING;
    
    // Reset auth attempt counter for new registration
    auth_attempt_count = 0;

    struct sockaddr_in server_addr;

    // DNS lookup (no watchdog needed - SIP task not monitored)
    char dns_msg[128];
    snprintf(dns_msg, sizeof(dns_msg), "Performing DNS lookup for %s:%d",
             sip_config.server, sip_config.port);
    sip_add_log_entry("info", dns_msg);
    
    if (!resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        sip_add_log_entry("error", "DNS lookup failed - cannot resolve hostname");
        current_state = SIP_STATE_ERROR;
        return false;
    }
    
    sip_add_log_entry("info", "DNS lookup successful - server resolved");

    // Get local IP address
    char local_ip[16];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        strcpy(local_ip, "192.168.1.100"); // Fallback
        ESP_LOGW(TAG, "Using fallback IP address");
    } else {
        ESP_LOGI(TAG, "Using local IP: %s", local_ip);
    }

    // Create REGISTER message (use static to avoid stack allocation)
    static char register_msg[1024];
    int branch_id = rand();
    int from_tag = rand();
    int call_id = rand();
    
    // Store Call-ID and From tag for reuse in authenticated REGISTER
    snprintf(initial_call_id, sizeof(initial_call_id), "%d@%s", call_id, local_ip);
    snprintf(initial_from_tag, sizeof(initial_from_tag), "%d", from_tag);
    has_initial_transaction_ids = true;
    
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Initial REGISTER: Call-ID=%s, From-tag=%s",
             initial_call_id, initial_from_tag);
    sip_add_log_entry("info", log_msg);
    
    snprintf(register_msg, sizeof(register_msg),
             "REGISTER sip:%s SIP/2.0\r\n"
             "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
             "Max-Forwards: 70\r\n"
             "From: <sip:%s@%s>;tag=%s\r\n"
             "To: <sip:%s@%s>\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 1 REGISTER\r\n"
             "Contact: <sip:%s@%s:5060>\r\n"
             "Expires: 3600\r\n"
             "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
             "User-Agent: ESP32-Doorbell/1.0\r\n"
             "Content-Length: 0\r\n\r\n",
             sip_config.server, local_ip, branch_id,
             sip_config.username, sip_config.server, initial_from_tag,
             sip_config.username, sip_config.server,
             initial_call_id,
             sip_config.username, local_ip);

    int sent = sendto(sip_socket, register_msg, strlen(register_msg), 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "Error sending REGISTER message: %d", sent);
        ESP_LOGE(TAG, "%s", err_msg);
        current_state = SIP_STATE_ERROR;
        return false;
    }

    last_message_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    sip_add_log_entry("sent", "REGISTER message sent");
    return true;
}

// Send authenticated REGISTER with digest authentication
static bool sip_client_register_auth(sip_auth_challenge_t* challenge)
{
    if (!sip_config.configured || sip_socket < 0 || !challenge || !challenge->valid) {
        current_state = SIP_STATE_ERROR;
        return false;
    }

    // Check if we have stored transaction IDs from initial REGISTER
    if (!has_initial_transaction_ids) {
        sip_add_log_entry("error", "No initial transaction IDs stored - cannot authenticate");
        current_state = SIP_STATE_ERROR;
        return false;
    }

    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "Sending authenticated REGISTER (reusing Call-ID=%s, From-tag=%s)",
             initial_call_id, initial_from_tag);
    sip_add_log_entry("info", log_msg);

    struct sockaddr_in server_addr;

    if (!resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        current_state = SIP_STATE_ERROR;
        return false;
    }

    // Get local IP
    char local_ip[16];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        strcpy(local_ip, "192.168.1.100");
    }

    // Generate cnonce and nc
    char cnonce[17];
    char nc_str[12];
    // Increment nonce count for each authentication attempt
    int nc_value = auth_attempt_count + 1;
    if (nc_value > 99999999) nc_value = 1; // Prevent overflow
    sprintf(nc_str, "%08d", nc_value);
    nc_str[8] = '\0'; // Ensure null termination (8 digits + null = 9 chars)
    generate_cnonce(cnonce, sizeof(cnonce));

    // Add debug logging for REGISTER authentication parameters
    char debug_msg[256];
    snprintf(debug_msg, sizeof(debug_msg), "REGISTER Auth attempt %d: nonce='%s', nc='%s', cnonce='%s'",
             auth_attempt_count + 1, challenge->nonce, nc_str, cnonce);
    sip_add_log_entry("info", debug_msg);

    // Calculate digest response
    char response[33];
    char register_uri[256];
    // For REGISTER, the URI should be "sip:domain" format
    snprintf(register_uri, sizeof(register_uri), "sip:%s", sip_config.server);
    calculate_digest_response(
        sip_config.username,
        sip_config.password,
        challenge->realm,
        challenge->nonce,
        "REGISTER",
        register_uri,
        challenge->qop,
        nc_str,
        cnonce,
        response
    );

    // Log the calculated response for debugging
    char response_debug[128];
    snprintf(response_debug, sizeof(response_debug), "REGISTER digest response: %s", response);
    sip_add_log_entry("info", response_debug);
    
    // Log digest calculation for debugging
    snprintf(log_msg, sizeof(log_msg), "Digest calculated: response=%s", response);
    sip_add_log_entry("info", log_msg);

    // Build authenticated REGISTER message (use static to avoid stack allocation)
    static char register_msg[1536];
    int branch = rand();  // New branch for new transaction
    
    // CRITICAL FIX: Build Authorization header WITHOUT spaces after commas
    // This matches the softphone format exactly
    int len = snprintf(register_msg, sizeof(register_msg),
        "REGISTER sip:%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%s\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %s\r\n"
        "CSeq: 2 REGISTER\r\n"
        "Contact: <sip:%s@%s:5060>\r\n"
        "Authorization: Digest username=\"%s\",realm=\"%s\",nonce=\"%s\",uri=\"sip:%s\",response=\"%s\"",
        sip_config.server, local_ip, branch,
        sip_config.username, sip_config.server, initial_from_tag,  // REUSE stored tag
        sip_config.username, sip_config.server,
        initial_call_id,  // REUSE stored Call-ID
        sip_config.username, local_ip,
        sip_config.username, challenge->realm, challenge->nonce, sip_config.server, response
    );

    // Add qop parameters if present (NO spaces after commas)
    if (strlen(challenge->qop) > 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ",qop=%s,nc=%s,cnonce=\"%s\"", challenge->qop, nc_str, cnonce);
    }

    // Add opaque if present (NO space after comma)
    if (strlen(challenge->opaque) > 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ",opaque=\"%s\"", challenge->opaque);
    }

    // Add algorithm if not MD5 (NO space after comma)
    if (strlen(challenge->algorithm) > 0 && strcmp(challenge->algorithm, "MD5") != 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ",algorithm=%s", challenge->algorithm);
    }

    // Complete the message
    len += snprintf(register_msg + len, sizeof(register_msg) - len,
        "\r\n"
        "Expires: 3600\r\n"
        "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
        "User-Agent: ESP32-Doorbell/1.0\r\n"
        "Content-Length: 0\r\n\r\n"
    );

    int sent = sendto(sip_socket, register_msg, len, 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Error sending authenticated REGISTER: %d", sent);
        sip_add_log_entry("error", "Failed to send authenticated REGISTER");
        current_state = SIP_STATE_ERROR;
        return false;
    }

    last_message_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    snprintf(log_msg, sizeof(log_msg), "Authenticated REGISTER sent (%d bytes)", sent);
    sip_add_log_entry("info", log_msg);
    return true;
}

void sip_client_make_call(const char* uri)
{
    // Allow calls only when IDLE or REGISTERED, or when retrying with authentication
    if (current_state != SIP_STATE_IDLE && current_state != SIP_STATE_REGISTERED &&
        !(has_invite_auth_challenge && invite_auth_attempt_count < MAX_INVITE_AUTH_ATTEMPTS)) {
        char state_log[128];
        snprintf(state_log, sizeof(state_log),
                 "Cannot make call - current state: %s, auth_count: %d",
                 (current_state < sizeof(state_names)/sizeof(state_names[0])) ?
                 state_names[current_state] : "UNKNOWN",
                 invite_auth_attempt_count);
        sip_add_log_entry("error", state_log);
        return;
    }

    if (!sip_config.configured) {
        sip_add_log_entry("error", "Cannot make call - SIP not configured");
        return;
    }

    if (sip_socket < 0) {
        sip_add_log_entry("error", "Cannot make call - socket not available");
        return;
    }

    // Format URI if needed (add sip: prefix if missing)
    static char formatted_uri[128];
    if (strncmp(uri, "sip:", 4) != 0) {
        // URI doesn't have sip: prefix, add it
        if (strchr(uri, '@') != NULL) {
            // Has @ symbol, use as-is with sip: prefix
            snprintf(formatted_uri, sizeof(formatted_uri), "sip:%s", uri);
        } else {
            // No @ symbol, add @server
            snprintf(formatted_uri, sizeof(formatted_uri), "sip:%s@%s", uri, sip_config.server);
        }
        uri = formatted_uri;
    }

    char log_msg[256];  // Increased buffer size to accommodate full URI
    snprintf(log_msg, sizeof(log_msg), "Initiating call to %s", uri);
    sip_add_log_entry("info", log_msg);

    // Only change state and set timestamp for initial call, not for auth retries
    if (invite_auth_attempt_count == 0) {
        current_state = SIP_STATE_CALLING;
        call_start_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    // Get local IP address
    char local_ip[16];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        strcpy(local_ip, "192.168.1.100"); // Fallback
    }

    // Create SDP session description
    static char sdp[256];
    snprintf(sdp, sizeof(sdp),
             "v=0\r\n"
             "o=- %d 0 IN IP4 %s\r\n"
             "s=ESP32 Doorbell Call\r\n"
             "c=IN IP4 %s\r\n"
             "t=0 0\r\n"
             "m=audio 5004 RTP/AVP 0 8 101\r\n"
             "a=rtpmap:0 PCMU/8000\r\n"
             "a=rtpmap:8 PCMA/8000\r\n"
             "a=rtpmap:101 telephone-event/8000\r\n"
             "a=fmtp:101 0-15\r\n"
             "a=sendrecv\r\n",
             rand(), local_ip, local_ip);

    // Create INVITE message (large buffer for authenticated INVITE with long URIs)
    static char invite_msg[3072];
    
    // CRITICAL: Store initial INVITE transaction IDs for authentication retry
    // Fritz!Box and other strict SIP servers require consistency
    static int initial_invite_call_id = 0;
    static int initial_invite_from_tag = 0;
    static int initial_invite_branch = 0;
    static int auth_invite_branch = 0;  // Separate branch for authenticated INVITE
    static int initial_invite_cseq = 1;
    
    // CRITICAL FIX: Only generate NEW IDs if this is a FRESH call (no auth challenge exists)
    // If we have an auth challenge, we're retrying and must REUSE the stored IDs
    if (!has_invite_auth_challenge) {
        // This is a fresh call - generate new transaction IDs
        initial_invite_call_id = rand();
        initial_invite_from_tag = rand();
        initial_invite_branch = rand();
        auth_invite_branch = 0;  // Will be set when auth retry happens
        initial_invite_cseq = 1;  // First INVITE always uses CSeq 1
        
        char init_log[256];
        snprintf(init_log, sizeof(init_log),
                 "INVITE fresh call: generating NEW Call-ID=%d, From-tag=%d, Branch=%d, CSeq=%d",
                 initial_invite_call_id, initial_invite_from_tag, initial_invite_branch, initial_invite_cseq);
        sip_add_log_entry("info", init_log);
    } else {
        // This is an auth retry - REUSE stored transaction IDs
        // Generate NEW branch for authenticated INVITE (per RFC 3261)
        auth_invite_branch = rand();
        
        char reuse_log[256];
        snprintf(reuse_log, sizeof(reuse_log),
                 "INVITE auth retry: REUSING Call-ID=%d, From-tag=%d (CSeq=%d), NEW Branch=%d",
                 initial_invite_call_id, initial_invite_from_tag, initial_invite_cseq, auth_invite_branch);
        sip_add_log_entry("info", reuse_log);
    }
    
    int call_id = initial_invite_call_id;
    int tag = initial_invite_from_tag;
    int branch = has_invite_auth_challenge ? auth_invite_branch : initial_invite_branch;

    // Check if we have stored authentication challenge for INVITE
    if (has_invite_auth_challenge && invite_auth_challenge.valid && invite_auth_attempt_count < MAX_INVITE_AUTH_ATTEMPTS) {
        sip_add_log_entry("info", "Using stored INVITE authentication challenge for retry");
        // Generate cnonce and nc for INVITE authentication
        char cnonce[17];
        char nc_str[12];
        // Increment nonce count for each authentication attempt
        int nc_value = invite_auth_attempt_count + 1;
        if (nc_value > 99999999) nc_value = 1; // Prevent overflow
        sprintf(nc_str, "%08d", nc_value);
        nc_str[8] = '\0'; // Ensure null termination (8 digits + null = 9 chars)
        generate_cnonce(cnonce, sizeof(cnonce));

        // Add debug logging for authentication parameters
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg),
                 "INVITE Auth retry %d: nonce='%.20s...', nc='%s', cnonce='%s', Call-ID=%d, From-tag=%d",
                 invite_auth_attempt_count + 1, invite_auth_challenge.nonce, nc_str, cnonce,
                 call_id, tag);
        sip_add_log_entry("info", debug_msg);

        // Calculate digest response for INVITE
        char response[33];
        // CRITICAL FIX: The digest URI MUST match the Request-URI (the target being called)
        // Use the actual target URI, not our own username
        // Limit to 64 bytes to satisfy format-truncation checks
        char invite_uri_for_digest[64];
        strncpy(invite_uri_for_digest, uri, sizeof(invite_uri_for_digest) - 1);
        invite_uri_for_digest[sizeof(invite_uri_for_digest) - 1] = '\0';
        
        // Log complete digest calculation inputs for debugging
        char digest_inputs[512];
        snprintf(digest_inputs, sizeof(digest_inputs),
                 "INVITE digest inputs: username=%s, realm=%s, method=INVITE, uri=%s, qop=%s, nc=%s",
                 sip_config.username, invite_auth_challenge.realm, invite_uri_for_digest,
                 invite_auth_challenge.qop[0] ? invite_auth_challenge.qop : "(empty)", nc_str);
        sip_add_log_entry("info", digest_inputs);

        calculate_digest_response(
            sip_config.username,
            sip_config.password,
            invite_auth_challenge.realm,
            invite_auth_challenge.nonce,
            "INVITE",
            invite_uri_for_digest,  // Use target URI for digest calculation
            invite_auth_challenge.qop,
            nc_str,
            cnonce,
            response
        );

        // Log the calculated response for debugging
        char response_debug[128];
        snprintf(response_debug, sizeof(response_debug), "INVITE digest response: %s", response);
        sip_add_log_entry("info", response_debug);
        
        // CRITICAL: Log CSeq value being used for debugging
        // RFC 3261 Section 8.1.3.5: CSeq should remain SAME for auth retry
        char cseq_log[128];
        snprintf(cseq_log, sizeof(cseq_log),
                 "INVITE CSeq for auth retry: %d (should be SAME as initial, not incremented)",
                 initial_invite_cseq);
        sip_add_log_entry("info", cseq_log);

        // Build INVITE message with Authorization header
        // Using truncated URIs to satisfy compiler format-truncation checks
        char safe_uri[64];
        strncpy(safe_uri, uri, sizeof(safe_uri) - 1);
        safe_uri[sizeof(safe_uri) - 1] = '\0';
        
        int len = 0;
        len = snprintf(invite_msg, sizeof(invite_msg),
            "INVITE %s SIP/2.0\r\n"
            "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
            "Max-Forwards: 70\r\n"
            "From: <sip:%s@%s>;tag=%d\r\n"
            "To: <%s>\r\n"
            "Call-ID: %d@%s\r\n"
            "CSeq: %d INVITE\r\n"
            "Contact: <sip:%s@%s:5060>\r\n"
            "Authorization: Digest username=\"%s\",realm=\"%s\",nonce=\"%s\",uri=\"%s\",response=\"%s\"",
            safe_uri, local_ip, branch,
            sip_config.username, sip_config.server, tag,
            safe_uri,
            call_id, local_ip,
            initial_invite_cseq,  // CRITICAL FIX: Use SAME CSeq as initial INVITE, not incremented
            sip_config.username, local_ip,
            sip_config.username, invite_auth_challenge.realm, invite_auth_challenge.nonce, invite_uri_for_digest, response
        );

        // Add qop parameters if present (NO spaces after commas)
        if (strlen(invite_auth_challenge.qop) > 0) {
            len += snprintf(invite_msg + len, sizeof(invite_msg) - len,
                ",qop=%s,nc=%s,cnonce=\"%s\"", invite_auth_challenge.qop, nc_str, cnonce);
        }

        // Add opaque if present (NO space after comma)
        if (strlen(invite_auth_challenge.opaque) > 0) {
            len += snprintf(invite_msg + len, sizeof(invite_msg) - len,
                ",opaque=\"%s\"", invite_auth_challenge.opaque);
        }

        // Add algorithm if not MD5 (NO space after comma)
        if (strlen(invite_auth_challenge.algorithm) > 0 && strcmp(invite_auth_challenge.algorithm, "MD5") != 0) {
            len += snprintf(invite_msg + len, sizeof(invite_msg) - len,
                ",algorithm=%s", invite_auth_challenge.algorithm);
        }

        // Complete the message
        len += snprintf(invite_msg + len, sizeof(invite_msg) - len,
            "\r\n"
            "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
            "User-Agent: ESP32-Doorbell/1.0\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n\r\n%s",
            strlen(sdp), sdp);

        invite_auth_attempt_count++;
        sip_add_log_entry("info", "Sending authenticated INVITE");
        
    } else {
        // Build INVITE message without authentication (first attempt)
        snprintf(invite_msg, sizeof(invite_msg),
            "INVITE %s SIP/2.0\r\n"
            "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
            "Max-Forwards: 70\r\n"
            "From: <sip:%s@%s>;tag=%d\r\n"
            "To: <%s>\r\n"
            "Call-ID: %d@%s\r\n"
            "CSeq: %d INVITE\r\n"
            "Contact: <sip:%s@%s:5060>\r\n"
            "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
            "User-Agent: ESP32-Doorbell/1.0\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n\r\n%s",
            uri, local_ip, branch,
            sip_config.username, sip_config.server, tag,
            uri,
            call_id, local_ip,
            initial_invite_cseq,  // Use stored CSeq value
            sip_config.username, local_ip,
            strlen(sdp), sdp);
    }

    // Resolve server address
    struct sockaddr_in server_addr;

    if (!resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
        sip_add_log_entry("error", "DNS lookup failed for call");
        current_state = SIP_STATE_ERROR;
        return;
    }

    // Send INVITE message
    int sent = sendto(sip_socket, invite_msg, strlen(invite_msg), 0,
                      (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Error sending INVITE message: %d", sent);
        sip_add_log_entry("error", "Failed to send INVITE");
        current_state = SIP_STATE_ERROR;
        return;
    }

    last_message_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Reuse log_msg buffer for INVITE sent message
    snprintf(log_msg, sizeof(log_msg), "INVITE sent to %s (%d bytes)", uri, sent);
    sip_add_log_entry("sent", log_msg);
}

void sip_client_hangup(void)
{
    if (current_state == SIP_STATE_CONNECTED || current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
        ESP_LOGI(TAG, "Ending call");
        sip_add_log_entry("info", "Sending BYE to end call");
        
        // Clear INVITE authentication state when call ends
        has_invite_auth_challenge = false;
        memset(&invite_auth_challenge, 0, sizeof(invite_auth_challenge));
        invite_auth_attempt_count = 0;
        
        // Stop audio and RTP first
        audio_stop_recording();
        audio_stop_playback();
        rtp_stop_session();
        
        // Send BYE message if we have an active call
        if (current_state == SIP_STATE_CONNECTED && sip_socket >= 0) {
            static char bye_msg[512];
            char local_ip[16];
            if (!get_local_ip(local_ip, sizeof(local_ip))) {
                strcpy(local_ip, "192.168.1.100");
            }
            
            int call_id = rand();
            int tag = rand();
            int branch = rand();
            
            snprintf(bye_msg, sizeof(bye_msg),
                    "BYE sip:%s@%s SIP/2.0\r\n"
                    "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d\r\n"
                    "Max-Forwards: 70\r\n"
                    "From: <sip:%s@%s>;tag=%d\r\n"
                    "To: <sip:%s@%s>\r\n"
                    "Call-ID: %d@%s\r\n"
                    "CSeq: 2 BYE\r\n"
                    "User-Agent: ESP32-Doorbell/1.0\r\n"
                    "Content-Length: 0\r\n\r\n",
                    sip_config.username, sip_config.server,
                    local_ip, branch,
                    sip_config.username, sip_config.server, tag,
                    sip_config.username, sip_config.server,
                    call_id, local_ip);
            
            // Send BYE
            struct sockaddr_in server_addr;
            if (resolve_hostname(sip_config.server, &server_addr, (uint16_t)sip_config.port)) {
                int sent = sendto(sip_socket, bye_msg, strlen(bye_msg), 0,
                                 (struct sockaddr*)&server_addr, sizeof(server_addr));
                if (sent > 0) {
                    sip_add_log_entry("sent", "BYE message sent");
                } else {
                    sip_add_log_entry("error", "Failed to send BYE");
                }
            }
        } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
            // Send CANCEL for calls that haven't been answered yet
            sip_add_log_entry("info", "Canceling outgoing call");
            // CANCEL message implementation would go here
        }
        
        // Return to registered state
        current_state = SIP_STATE_REGISTERED;
        call_start_timestamp = 0; // Clear timeout
        
        // Reset DTMF decoder state when call ends
        dtmf_reset_call_state();
        
        sip_add_log_entry("info", "Call ended - State: REGISTERED");
    }
}

void sip_client_answer_call(void)
{
    // This function is now a placeholder - incoming calls are answered automatically
    // in the SIP task when INVITE is received
    if (current_state == SIP_STATE_RINGING || current_state == SIP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Call already answered or in progress");
    } else {
        ESP_LOGW(TAG, "No incoming call to answer");
    }
}

void sip_client_send_dtmf(char dtmf_digit)
{
    if (current_state == SIP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Sending DTMF: %c", dtmf_digit);
        sip_add_log_entry("info", "Sending DTMF - State: DTMF_SENDING");
        current_state = SIP_STATE_DTMF_SENDING;

        // DTMF sending logic would go here
        // For now, we'll just log it and return to connected state
        ESP_LOGI(TAG, "DTMF %c sent", dtmf_digit);

        // Return to connected state after DTMF
        current_state = SIP_STATE_CONNECTED;
        sip_add_log_entry("info", "DTMF sent - State: CONNECTED");
    } else {
        ESP_LOGW(TAG, "Cannot send DTMF - Status: %d", current_state);
    }
}

bool sip_client_test_connection(void)
{
    ESP_LOGI(TAG, "Testing SIP connection");

    if (!sip_config.configured) {
        ESP_LOGE(TAG, "No SIP configuration available");
        return false;
    }

    if (sip_socket < 0) {
        ESP_LOGE(TAG, "SIP socket not available");
        return false;
    }

    // For testing purposes, we'll just check if we can resolve the hostname
    struct sockaddr_in test_addr;
    if (!resolve_hostname(sip_config.server, &test_addr, (uint16_t)sip_config.port)) {
        ESP_LOGE(TAG, "Cannot resolve hostname: %s", sip_config.server);
        return false;
    }

    char reachable_msg[128];
    snprintf(reachable_msg, sizeof(reachable_msg), "SIP server %s is reachable", sip_config.server);
    ESP_LOGI(TAG, "%s", reachable_msg);
    return true;
}

void sip_get_status(char* buffer, size_t buffer_size)
{
    sip_state_t state = sip_client_get_state();
    const char* state_name = (state < sizeof(state_names)/sizeof(state_names[0])) ? 
                            state_names[state] : "UNKNOWN";

    // Determine user-friendly status
    const char* user_status;
    if (state == SIP_STATE_REGISTERED || state == SIP_STATE_CONNECTED) {
        user_status = "Registered";
    } else if (state == SIP_STATE_REGISTERING) {
        user_status = "Connecting";
    } else if (state == SIP_STATE_AUTH_FAILED) {
        user_status = "Authentication Failed";
    } else if (state == SIP_STATE_NETWORK_ERROR) {
        user_status = "Network Error";
    } else if (state == SIP_STATE_TIMEOUT) {
        user_status = "Connection Timeout";
    } else if (state == SIP_STATE_ERROR) {
        user_status = "Error";
    } else if (!sip_config.configured) {
        user_status = "Not Configured";
    } else {
        user_status = "Not Registered";
    }

    char server[64] = {0};
    char username[32] = {0};
    char apt1[64] = {0};
    char apt2[64] = {0};

    // Safely copy strings to avoid buffer issues
    strncpy(server, sip_config.server, sizeof(server) - 1);
    strncpy(username, sip_config.username, sizeof(username) - 1);
    strncpy(apt1, sip_config.apartment1_uri, sizeof(apt1) - 1);
    strncpy(apt2, sip_config.apartment2_uri, sizeof(apt2) - 1);

    snprintf(buffer, buffer_size,
             "{"
             "\"state\": \"%s\","
             "\"status\": \"%s\","
             "\"state_code\": %d,"
             "\"configured\": %s,"
             "\"server\": \"%s\","
             "\"username\": \"%s\","
             "\"apartment1\": \"%s\","
             "\"apartment2\": \"%s\","
             "\"port\": %d"
             "}",
             state_name,
             user_status,
             state,
             sip_config.configured ? "true" : "false",
             server,
             username,
             apt1,
             apt2,
             sip_config.port);
}

sip_state_t sip_client_get_state(void)
{
    return current_state;
}

void sip_save_config(const char* server, const char* username, const char* password,
                     const char* apt1, const char* apt2, int port)
{
    char save_msg[128];
    snprintf(save_msg, sizeof(save_msg), "Saving SIP configuration: %s@%s", username, server);
    ESP_LOGI(TAG, "%s", save_msg);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("sip_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        // Save SIP configuration fields
        nvs_set_str(nvs_handle, "server", server);
        nvs_set_str(nvs_handle, "username", username);
        nvs_set_str(nvs_handle, "password", password);
        nvs_set_str(nvs_handle, "apt1", apt1);
        nvs_set_str(nvs_handle, "apt2", apt2);
        nvs_set_u16(nvs_handle, "port", (uint16_t)port);
        nvs_set_u8(nvs_handle, "configured", 1);

        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "SIP configuration saved");
    } else {
        ESP_LOGE(TAG, "Error opening NVS handle: %d", err);
    }
}

sip_config_t sip_load_config(void)
{
    sip_config_t config = {0};
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("sip_config", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        uint8_t configured = 0;
        
        nvs_get_u8(nvs_handle, "configured", &configured);
        if (configured) {
            required_size = sizeof(config.server);
            nvs_get_str(nvs_handle, "server", config.server, &required_size);
            
            required_size = sizeof(config.username);
            nvs_get_str(nvs_handle, "username", config.username, &required_size);
            
            required_size = sizeof(config.password);
            nvs_get_str(nvs_handle, "password", config.password, &required_size);
            
            required_size = sizeof(config.apartment1_uri);
            nvs_get_str(nvs_handle, "apt1", config.apartment1_uri, &required_size);
            
            required_size = sizeof(config.apartment2_uri);
            nvs_get_str(nvs_handle, "apt2", config.apartment2_uri, &required_size);
            
            uint16_t port_val = 5060;
            nvs_get_u16(nvs_handle, "port", &port_val);
            config.port = (int)port_val;
            
            config.configured = true;
        }
        nvs_close(nvs_handle);
    }
    
    return config;
}

// Get log entries for web interface
int sip_get_log_entries(sip_log_entry_t* entries, int max_entries, uint64_t since_timestamp)
{
    if (!sip_log_mutex || !entries) {
        return 0;
    }
    
    int count = 0;
    
    if (xSemaphoreTake(sip_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int start_index = (sip_log_write_index - sip_log_count + SIP_LOG_MAX_ENTRIES) % SIP_LOG_MAX_ENTRIES;
        
        int i = 0;
        while (i < sip_log_count && count < max_entries) {
            int index = (start_index + i) % SIP_LOG_MAX_ENTRIES;
            // Use > to exclude entries at exactly since_timestamp (already seen)
            if (sip_log_buffer[index].timestamp > since_timestamp) {
                memcpy(&entries[count], &sip_log_buffer[index], sizeof(sip_log_entry_t));
                count++;
            }
            i++;
        }
        
        xSemaphoreGive(sip_log_mutex);
    }
    
    return count;
}

// Additional functions for web interface
bool sip_is_registered(void)
{
    return (current_state == SIP_STATE_REGISTERED ||
            current_state == SIP_STATE_CALLING ||
            current_state == SIP_STATE_RINGING ||
            current_state == SIP_STATE_CONNECTED ||
            current_state == SIP_STATE_DTMF_SENDING);
}

const char* sip_get_server(void)
{
    return sip_config.server;
}

const char* sip_get_username(void)
{
    return sip_config.username;
}

const char* sip_get_password(void)
{
    return sip_config.password;
}

const char* sip_get_target1(void)
{
    return sip_config.apartment1_uri;
}

const char* sip_get_target2(void)
{
    return sip_config.apartment2_uri;
}

void sip_set_server(const char* server)
{
    if (server) {
        strncpy(sip_config.server, server, sizeof(sip_config.server) - 1);
        sip_config.server[sizeof(sip_config.server) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_set_username(const char* username)
{
    if (username) {
        strncpy(sip_config.username, username, sizeof(sip_config.username) - 1);
        sip_config.username[sizeof(sip_config.username) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_set_password(const char* password)
{
    if (password) {
        strncpy(sip_config.password, password, sizeof(sip_config.password) - 1);
        sip_config.password[sizeof(sip_config.password) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_set_target1(const char* target)
{
    if (target) {
        strncpy(sip_config.apartment1_uri, target, sizeof(sip_config.apartment1_uri) - 1);
        sip_config.apartment1_uri[sizeof(sip_config.apartment1_uri) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_set_target2(const char* target)
{
    if (target) {
        strncpy(sip_config.apartment2_uri, target, sizeof(sip_config.apartment2_uri) - 1);
        sip_config.apartment2_uri[sizeof(sip_config.apartment2_uri) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_reinit(void)
{
    ESP_LOGI(TAG, "SIP reinitialization requested");
    sip_add_log_entry("info", "SIP reinitialization requested");
    
    // Set flag to trigger reinit from SIP task context (has more stack)
    // Don't do heavy operations from HTTP handler context
    reinit_requested = true;
}

bool sip_test_configuration(void)
{
    ESP_LOGI(TAG, "Testing SIP configuration");
    sip_add_log_entry("info", "Testing SIP configuration");

    if (!sip_config.configured) {
        ESP_LOGE(TAG, "No SIP configuration available for testing");
        sip_add_log_entry("error", "No SIP configuration available");
        return false;
    }

    ESP_LOGI(TAG, "Testing SIP server: %s:%d", sip_config.server, sip_config.port);
    ESP_LOGI(TAG, "Testing SIP user: %s", sip_config.username);

    // Simple validation - just check if configuration is present
    // Don't do DNS lookup or network operations here to avoid blocking
    if (strlen(sip_config.server) == 0 || strlen(sip_config.username) == 0) {
        ESP_LOGE(TAG, "Invalid SIP configuration");
        sip_add_log_entry("error", "Invalid SIP configuration");
        return false;
    }

    ESP_LOGI(TAG, "SIP configuration validation passed");
    sip_add_log_entry("info", "SIP configuration validation passed");
    return true;
}

// Connect to SIP server (start registration)
bool sip_connect(void)
{
    sip_add_log_entry("info", "SIP connect requested");
    
    if (!sip_config.configured) {
        ESP_LOGE(TAG, "Cannot connect: SIP not configured");
        sip_add_log_entry("error", "Cannot connect: SIP not configured");
        return false;
    }
    
    if (current_state == SIP_STATE_REGISTERED) {
        sip_add_log_entry("info", "Already registered to SIP server");
        return true;
    }
    
    // If disconnected, change state to idle so registration can proceed
    if (current_state == SIP_STATE_DISCONNECTED) {
        current_state = SIP_STATE_IDLE;
        sip_add_log_entry("info", "State changed from DISCONNECTED to IDLE - Reconnecting");
    }
    
    // Set flag to trigger registration in SIP task (non-blocking)
    registration_requested = true;
    sip_add_log_entry("info", "SIP registration queued");
    
    return true;
}

// Disconnect from SIP server
void sip_disconnect(void)
{
    sip_add_log_entry("info", "SIP disconnect requested");
    
    // Send REGISTER with Expires: 0 to unregister (if registered)
    if (current_state == SIP_STATE_REGISTERED && sip_socket >= 0) {
        sip_add_log_entry("info", "Sending unregister message");
        // Unregister implementation would send REGISTER with Expires: 0
    }
    
    // Close socket
    if (sip_socket >= 0) {
        close(sip_socket);
        sip_socket = -1;
        sip_add_log_entry("info", "SIP socket closed");
    }
    
    // Clear registration flag
    registration_requested = false;
    
    // Update state
    current_state = SIP_STATE_DISCONNECTED;
    sip_add_log_entry("info", "SIP client disconnected");
}

#pragma GCC diagnostic pop
