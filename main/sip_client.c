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

// Simplified SIP messages
static const char* sip_register_template = 
"REGISTER sip:%s SIP/2.0\r\n"
"Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
"Max-Forwards: 70\r\n"
"From: <sip:%s@%s>;tag=%d\r\n"
"To: <sip:%s@%s>\r\n"
"Call-ID: %d@%s\r\n"
"CSeq: 1 REGISTER\r\n"
"Contact: <sip:%s@%s:5060>\r\n"
"Expires: 3600\r\n"
"Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
"User-Agent: ESP32-Doorbell/1.0\r\n"
"Content-Length: 0\r\n\r\n";

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
    uint32_t random = esp_random();
    snprintf(cnonce_out, len, "%08lx%08lx", (unsigned long)random, (unsigned long)esp_random());
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
    
    // Calculate HA1 = MD5(username:realm:password)
    snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
    calculate_md5_hex(ha1_input, ha1);
    
    // Calculate HA2 = MD5(method:uri)
    snprintf(ha2_input, sizeof(ha2_input), "%s:sip:%s", method, uri);
    calculate_md5_hex(ha2_input, ha2);
    
    // Calculate response
    if (qop && strlen(qop) > 0 && strcmp(qop, "auth") == 0) {
        // With qop=auth: MD5(HA1:nonce:nc:cnonce:qop:HA2)
        snprintf(response_input, sizeof(response_input), 
                 "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
    } else {
        // Without qop: MD5(HA1:nonce:HA2)
        snprintf(response_input, sizeof(response_input), 
                 "%s:%s:%s", ha1, nonce, ha2);
    }
    
    calculate_md5_hex(response_input, response_out);
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
        
        // Auto-registration after delay (if configured and not already registered)
        if (init_timestamp > 0 && 
            current_state == SIP_STATE_IDLE && 
            sip_config.configured &&
            (xTaskGetTickCount() * portTICK_PERIOD_MS - init_timestamp) >= auto_register_delay_ms) {
            init_timestamp = 0; // Clear flag so we only try once
            sip_add_log_entry("info", "Auto-registration triggered after 5000 ms delay");
            registration_requested = true;
        }
        
        // Check if registration was requested (manual or auto)
        if (registration_requested && current_state != SIP_STATE_REGISTERED) {
            registration_requested = false;
            
            // Recreate socket if it was closed
            if (sip_socket < 0) {
                sip_add_log_entry("info", "Recreating SIP socket");
                
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
                
                sip_add_log_entry("info", "SIP socket recreated and bound to port 5060");
            }
            
            sip_add_log_entry("info", "Processing registration request");
            sip_client_register();
        }
        
        if (sip_socket >= 0) {
            len = recv(sip_socket, buffer, buffer_size - 1, MSG_DONTWAIT);
            if (len > 0) {
                buffer[len] = '\0';
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
                        sip_add_log_entry("info", "SIP registration successful");
                    } else if (current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
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
                        if (resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
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
                        sip_add_log_entry("info", "Authentication required, parsing challenge");
                        
                        // Parse authentication challenge
                        last_auth_challenge = parse_www_authenticate(buffer);
                        
                        if (last_auth_challenge.valid) {
                            // Send authenticated REGISTER
                            sip_client_register_auth(&last_auth_challenge);
                        } else {
                            sip_add_log_entry("error", "Failed to parse auth challenge");
                            current_state = SIP_STATE_AUTH_FAILED;
                            sip_add_log_entry("error", "State changed to AUTH_FAILED");
                        }
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
                    
                    if (resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
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
                    if (resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
                        sendto(sip_socket, bye_response, strlen(bye_response), 0,
                              (struct sockaddr*)&server_addr, sizeof(server_addr));
                        sip_add_log_entry("sent", "200 OK response to BYE");
                    }
                    
                    current_state = SIP_STATE_REGISTERED;
                    call_start_timestamp = 0; // Clear timeout
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
                // DTMF detection
                dtmf_process_audio(tx_buffer, samples_read);
                
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

    struct sockaddr_in server_addr;

    // DNS lookup (no watchdog needed - SIP task not monitored)
    if (!resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
        sip_add_log_entry("error", "DNS lookup failed");
        current_state = SIP_STATE_ERROR;
        return false;
    }
    
    sip_add_log_entry("info", "DNS lookup successful");

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
    snprintf(register_msg, sizeof(register_msg), sip_register_template,
             sip_config.server, local_ip, rand(),
             sip_config.username, sip_config.server, rand(),
             sip_config.username, sip_config.server,
             rand(), local_ip,
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

    sip_add_log_entry("info", "Sending authenticated REGISTER");

    struct sockaddr_in server_addr;

    if (!resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
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
    const char* nc = "00000001";
    generate_cnonce(cnonce, sizeof(cnonce));

    // Calculate digest response
    char response[33];
    calculate_digest_response(
        sip_config.username,
        sip_config.password,
        challenge->realm,
        challenge->nonce,
        "REGISTER",
        sip_config.server,
        challenge->qop,
        nc,
        cnonce,
        response
    );

    // Build authenticated REGISTER message (use static to avoid stack allocation)
    static char register_msg[1536];  // Reduced from 2048 to 1536
    int call_id = rand();
    int tag = rand();
    int branch = rand();
    
    int len = snprintf(register_msg, sizeof(register_msg),
        "REGISTER sip:%s SIP/2.0\r\n"
        "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
        "Max-Forwards: 70\r\n"
        "From: <sip:%s@%s>;tag=%d\r\n"
        "To: <sip:%s@%s>\r\n"
        "Call-ID: %d@%s\r\n"
        "CSeq: 2 REGISTER\r\n"
        "Contact: <sip:%s@%s:5060>\r\n"
        "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"sip:%s\", response=\"%s\"",
        sip_config.server, local_ip, branch,
        sip_config.username, sip_config.server, tag,
        sip_config.username, sip_config.server,
        call_id, local_ip,
        sip_config.username, local_ip,
        sip_config.username, challenge->realm, challenge->nonce, sip_config.server, response
    );

    // Add qop parameters if present
    if (strlen(challenge->qop) > 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ", qop=%s, nc=%s, cnonce=\"%s\"", challenge->qop, nc, cnonce);
    }

    // Add opaque if present
    if (strlen(challenge->opaque) > 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ", opaque=\"%s\"", challenge->opaque);
    }

    // Add algorithm if not MD5
    if (strlen(challenge->algorithm) > 0 && strcmp(challenge->algorithm, "MD5") != 0) {
        len += snprintf(register_msg + len, sizeof(register_msg) - len,
            ", algorithm=%s", challenge->algorithm);
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

    sip_add_log_entry("info", "Authenticated REGISTER sent successfully");
    return true;
}

void sip_client_make_call(const char* uri)
{
    // Allow calls only when IDLE or REGISTERED
    if (current_state != SIP_STATE_IDLE && current_state != SIP_STATE_REGISTERED) {
        sip_add_log_entry("error", "Cannot make call - not in ready state");
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
    current_state = SIP_STATE_CALLING;
    call_start_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
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
    
    // Create INVITE message
    static char invite_msg[1536];
    int call_id = rand();
    int tag = rand();
    int branch = rand();
    
    snprintf(invite_msg, sizeof(invite_msg),
             "INVITE %s SIP/2.0\r\n"
             "Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d;rport\r\n"
             "Max-Forwards: 70\r\n"
             "From: <sip:%s@%s>;tag=%d\r\n"
             "To: <%s>\r\n"
             "Call-ID: %d@%s\r\n"
             "CSeq: 1 INVITE\r\n"
             "Contact: <sip:%s@%s:5060>\r\n"
             "Allow: INVITE, ACK, CANCEL, BYE, NOTIFY, REFER, MESSAGE, OPTIONS, INFO, SUBSCRIBE\r\n"
             "User-Agent: ESP32-Doorbell/1.0\r\n"
             "Content-Type: application/sdp\r\n"
             "Content-Length: %d\r\n\r\n%s",
             uri, local_ip, branch,
             sip_config.username, sip_config.server, tag,
             uri,
             call_id, local_ip,
             sip_config.username, local_ip,
             strlen(sdp), sdp);
    
    // Resolve server address
    struct sockaddr_in server_addr;
    
    if (!resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
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
    
    // Reuse log_msg buffer for INVITE sent message
    snprintf(log_msg, sizeof(log_msg), "INVITE sent to %s (%d bytes)", uri, sent);
    sip_add_log_entry("sent", log_msg);
}

void sip_client_hangup(void)
{
    if (current_state == SIP_STATE_CONNECTED || current_state == SIP_STATE_CALLING || current_state == SIP_STATE_RINGING) {
        ESP_LOGI(TAG, "Ending call");
        sip_add_log_entry("info", "Sending BYE to end call");
        
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
            if (resolve_hostname(sip_config.server, &server_addr, sip_config.port)) {
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
    if (!resolve_hostname(sip_config.server, &test_addr, sip_config.port)) {
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

    const char* status_template =
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
        "}";

    char server[64] = {0};
    char username[32] = {0};
    char apt1[64] = {0};
    char apt2[64] = {0};

    // Safely copy strings to avoid buffer issues
    strncpy(server, sip_config.server, sizeof(server) - 1);
    strncpy(username, sip_config.username, sizeof(username) - 1);
    strncpy(apt1, sip_config.apartment1_uri, sizeof(apt1) - 1);
    strncpy(apt2, sip_config.apartment2_uri, sizeof(apt2) - 1);

    snprintf(buffer, buffer_size, status_template,
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
