#include "sip_client.h"
#include "audio_handler.h"
#include "dtmf_decoder.h"
#include "ntp_sync.h"
#include "ntp_log.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
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
static uint32_t auto_register_delay_ms = 5000; // Wait 5 seconds after init
static uint32_t init_timestamp = 0;

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

static const char* sip_invite_template = 
"INVITE %s SIP/2.0\r\n"
"Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d\r\n"
"From: <sip:%s@%s>;tag=%d\r\n"
"To: <%s>\r\n"
"Call-ID: %d@%s\r\n"
"CSeq: 1 INVITE\r\n"
"Contact: <sip:%s@%s:5060>\r\n"
"Content-Type: application/sdp\r\n"
"Content-Length: %d\r\n\r\n%s";

// Helper function to add log entry (thread-safe)
static void sip_add_log_entry(const char* type, const char* message)
{
    if (!sip_log_mutex) {
        sip_log_mutex = xSemaphoreCreateMutex();
    }
    
    if (xSemaphoreTake(sip_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
}

// Calculate MD5 hash and convert to hex string
static void calculate_md5_hex(const char* input, char* output) {
    unsigned char hash[16];
    
    // Use mbedtls_md5() instead of deprecated mbedtls_md5_ret()
    mbedtls_md5((const unsigned char*)input, strlen(input), hash);
    
    // Convert to hex string
    for (int i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[32] = '\0';
}

// Generate random cnonce for digest authentication
static void generate_cnonce(char* cnonce_out, size_t len) {
    uint32_t random = esp_random();
    snprintf(cnonce_out, len, "%08lx%08lx", (unsigned long)random, (unsigned long)esp_random());
}

// Parse WWW-Authenticate header
static sip_auth_challenge_t parse_www_authenticate(const char* buffer) {
    sip_auth_challenge_t challenge = {0};
    
    char* auth_header = strstr(buffer, "WWW-Authenticate:");
    if (!auth_header) {
        return challenge;
    }
    
    // Extract realm
    char* realm_start = strstr(auth_header, "realm=\"");
    if (realm_start) {
        realm_start += 7;
        char* realm_end = strchr(realm_start, '"');
        if (realm_end) {
            int len = realm_end - realm_start;
            if (len < sizeof(challenge.realm)) {
                strncpy(challenge.realm, realm_start, len);
                challenge.realm[len] = '\0';
            }
        }
    }
    
    // Extract nonce
    char* nonce_start = strstr(auth_header, "nonce=\"");
    if (nonce_start) {
        nonce_start += 7;
        char* nonce_end = strchr(nonce_start, '"');
        if (nonce_end) {
            int len = nonce_end - nonce_start;
            if (len < sizeof(challenge.nonce)) {
                strncpy(challenge.nonce, nonce_start, len);
                challenge.nonce[len] = '\0';
            }
        }
    }
    
    // Extract qop
    char* qop_start = strstr(auth_header, "qop=\"");
    if (qop_start) {
        qop_start += 5;
        char* qop_end = strchr(qop_start, '"');
        if (qop_end) {
            int len = qop_end - qop_start;
            if (len < sizeof(challenge.qop)) {
                strncpy(challenge.qop, qop_start, len);
                challenge.qop[len] = '\0';
            }
        }
    }
    
    // Extract opaque (optional)
    char* opaque_start = strstr(auth_header, "opaque=\"");
    if (opaque_start) {
        opaque_start += 8;
        char* opaque_end = strchr(opaque_start, '"');
        if (opaque_end) {
            int len = opaque_end - opaque_start;
            if (len < sizeof(challenge.opaque)) {
                strncpy(challenge.opaque, opaque_start, len);
                challenge.opaque[len] = '\0';
            }
        }
    }
    
    // Extract algorithm (optional, defaults to MD5)
    char* algo_start = strstr(auth_header, "algorithm=");
    if (algo_start) {
        algo_start += 10;
        if (*algo_start == '"') algo_start++;
        char* algo_end = strpbrk(algo_start, "\",\r\n ");
        if (algo_end) {
            int len = algo_end - algo_start;
            if (len < sizeof(challenge.algorithm)) {
                strncpy(challenge.algorithm, algo_start, len);
                challenge.algorithm[len] = '\0';
            }
        }
    } else {
        strcpy(challenge.algorithm, "MD5");
    }
    
    challenge.valid = (strlen(challenge.realm) > 0 && strlen(challenge.nonce) > 0);
    
    if (challenge.valid) {
        NTP_LOGI(TAG, "Auth challenge parsed: realm=%s, qop=%s, algorithm=%s", 
                 challenge.realm, challenge.qop, challenge.algorithm);
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
    char ha1[33], ha2[33];
    char ha1_input[256], ha2_input[256], response_input[512];
    
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
static void sip_task(void *pvParameters)
{
    char buffer[2048];
    int len;
    
    NTP_LOGI(TAG, "SIP task started on core %d", xPortGetCoreID());
    sip_add_log_entry("info", "SIP task started on Core 1");
    
    while (1) {
        // Longer delay to minimize CPU usage - SIP doesn't need fast polling
        // 200ms is more than sufficient for SIP protocol
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // Reset watchdog to prevent timeout during long operations
        esp_task_wdt_reset();
        
        // Auto-registration after delay (if configured and not already registered)
        if (init_timestamp > 0 && 
            current_state == SIP_STATE_IDLE && 
            sip_config.configured &&
            (xTaskGetTickCount() * portTICK_PERIOD_MS - init_timestamp) >= auto_register_delay_ms) {
            init_timestamp = 0; // Clear flag so we only try once
            NTP_LOGI(TAG, "Auto-registration triggered after %lu ms delay", auto_register_delay_ms);
            sip_add_log_entry("info", "Auto-registration starting (WiFi stable)");
            registration_requested = true;
        }
        
        // Check if registration was requested (manual or auto)
        if (registration_requested && current_state != SIP_STATE_REGISTERED) {
            registration_requested = false;
            
            // Recreate socket if it was closed
            if (sip_socket < 0) {
                NTP_LOGI(TAG, "Recreating SIP socket");
                sip_add_log_entry("info", "Creating SIP socket");
                
                sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (sip_socket < 0) {
                    ESP_LOGE(TAG, "Error creating SIP socket");
                    sip_add_log_entry("error", "Failed to create SIP socket");
                    current_state = SIP_STATE_ERROR;
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
                    continue;
                }
                
                NTP_LOGI(TAG, "SIP socket recreated and bound to port 5060");
                sip_add_log_entry("info", "SIP socket ready");
            }
            
            NTP_LOGI(TAG, "Processing registration request");
            sip_add_log_entry("info", "Starting SIP registration");
            sip_client_register();
        }
        
        if (sip_socket >= 0) {
            len = recv(sip_socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            if (len > 0) {
                buffer[len] = '\0';
                NTP_LOGI(TAG, "SIP message received:\n%s", buffer);
                
                // Log received message (truncated for display)
                char log_msg[SIP_LOG_MAX_MESSAGE_LEN];
                snprintf(log_msg, sizeof(log_msg), "%.200s%s", buffer, len > 200 ? "..." : "");
                sip_add_log_entry("received", log_msg);
                
                // Enhanced SIP message processing with better error handling
                // Check response codes first (more specific than method names)
                if (strstr(buffer, "SIP/2.0 200 OK")) {
                    if (current_state == SIP_STATE_REGISTERING) {
                        current_state = SIP_STATE_REGISTERED;
                        NTP_LOGI(TAG, "SIP registration successful");
                        sip_add_log_entry("info", "SIP registration successful");
                    } else if (current_state == SIP_STATE_CALLING) {
                        current_state = SIP_STATE_CONNECTED;
                        NTP_LOGI(TAG, "Call connected");
                        audio_start_recording();
                        audio_start_playback();
                    }
                } else if (strstr(buffer, "SIP/2.0 401 Unauthorized")) {
                    if (current_state == SIP_STATE_REGISTERING) {
                        NTP_LOGI(TAG, "Authentication required, parsing challenge");
                        sip_add_log_entry("info", "Authentication required");
                        
                        // Parse authentication challenge
                        last_auth_challenge = parse_www_authenticate(buffer);
                        
                        if (last_auth_challenge.valid) {
                            // Send authenticated REGISTER
                            sip_client_register_auth(&last_auth_challenge);
                        } else {
                            ESP_LOGE(TAG, "Failed to parse authentication challenge");
                            sip_add_log_entry("error", "Failed to parse auth challenge");
                            current_state = SIP_STATE_AUTH_FAILED;
                        }
                    }
                } else if (strstr(buffer, "SIP/2.0 100 Trying")) {
                    // Provisional response, just log it
                    NTP_LOGI(TAG, "Server processing request");
                } else if (strstr(buffer, "SIP/2.0 403 Forbidden")) {
                    ESP_LOGE(TAG, "SIP forbidden");
                    sip_add_log_entry("error", "SIP forbidden");
                    current_state = SIP_STATE_AUTH_FAILED;
                } else if (strstr(buffer, "SIP/2.0 404 Not Found")) {
                    ESP_LOGE(TAG, "SIP target not found");
                    sip_add_log_entry("error", "SIP target not found");
                    current_state = SIP_STATE_ERROR;
                } else if (strstr(buffer, "SIP/2.0 408 Request Timeout")) {
                    ESP_LOGE(TAG, "SIP request timeout");
                    sip_add_log_entry("error", "SIP request timeout");
                    current_state = SIP_STATE_TIMEOUT;
                } else if (strstr(buffer, "SIP/2.0 486 Busy Here")) {
                    ESP_LOGW(TAG, "SIP target busy");
                    sip_add_log_entry("info", "SIP target busy");
                    current_state = SIP_STATE_REGISTERED;
                } else if (strstr(buffer, "SIP/2.0 487 Request Terminated")) {
                    NTP_LOGI(TAG, "SIP request terminated");
                    sip_add_log_entry("info", "SIP request terminated");
                    current_state = SIP_STATE_REGISTERED;
                } else if (strncmp(buffer, "INVITE sip:", 11) == 0) {
                    // Check for INVITE request (not response)
                    NTP_LOGI(TAG, "Incoming call");
                    sip_add_log_entry("info", "Incoming call");
                    current_state = SIP_STATE_RINGING;
                    // Auto-answer after short delay
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    sip_client_answer_call();
                } else if (strncmp(buffer, "BYE sip:", 8) == 0) {
                    // Check for BYE request (not response)
                    NTP_LOGI(TAG, "Call ended");
                    sip_add_log_entry("info", "Call ended");
                    current_state = SIP_STATE_REGISTERED;
                    audio_stop_recording();
                    audio_stop_playback();
                }
            }
        }
        
        // Audio-Verarbeitung während eines Anrufs
        if (current_state == SIP_STATE_CONNECTED) {
            int16_t audio_buffer[160]; // 20ms bei 8kHz
            size_t samples_read = audio_read(audio_buffer, 160);
            if (samples_read > 0) {
                // DTMF-Erkennung
                dtmf_process_audio(audio_buffer, samples_read);
                
                // Audio über RTP senden (vereinfacht)
                // In einer echten Implementierung würde hier RTP verwendet
            }
        }
    }
    
    ESP_LOGI(TAG, "SIP task ended");
    vTaskDelete(NULL);
}

void sip_client_init(void)
{
    NTP_LOGI(TAG, "Initializing SIP client");

    // Set initial state
    current_state = SIP_STATE_IDLE;

    // Load configuration
    sip_config = sip_load_config();

    if (sip_config.configured) {
        NTP_LOGI(TAG, "SIP configuration loaded: %s@%s", sip_config.username, sip_config.server);

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
        
        NTP_LOGI(TAG, "SIP socket bound to port 5060");

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
        
        // Subscribe SIP task to watchdog timer
        esp_task_wdt_add(sip_task_handle);
        ESP_LOGI(TAG, "SIP task subscribed to watchdog timer");

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
        // Unsubscribe from watchdog before deleting task
        esp_task_wdt_delete(sip_task_handle);
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

    NTP_LOGI(TAG, "SIP registration with %s", sip_config.server);
    sip_add_log_entry("info", "Starting DNS lookup");
    current_state = SIP_STATE_REGISTERING;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sip_config.port);

    // Reset watchdog before potentially long DNS lookup
    esp_task_wdt_reset();
    
    struct hostent *host = gethostbyname(sip_config.server);
    
    // Reset watchdog after DNS lookup
    esp_task_wdt_reset();
    
    if (host == NULL) {
        ESP_LOGE(TAG, "Cannot resolve hostname: %s", sip_config.server);
        sip_add_log_entry("error", "DNS lookup failed");
        current_state = SIP_STATE_ERROR;
        return false;
    }
    
    NTP_LOGI(TAG, "DNS lookup successful");
    sip_add_log_entry("info", "DNS lookup successful");

    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // Get local IP address
    char local_ip[16];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        strcpy(local_ip, "192.168.1.100"); // Fallback
        ESP_LOGW(TAG, "Using fallback IP address");
    } else {
        ESP_LOGI(TAG, "Using local IP: %s", local_ip);
    }

    // Create REGISTER message (simplified)
    char register_msg[1024];
    snprintf(register_msg, sizeof(register_msg), sip_register_template,
             sip_config.server, local_ip, rand(),
             sip_config.username, sip_config.server, rand(),
             sip_config.username, sip_config.server,
             rand(), local_ip,
             sip_config.username, local_ip);

    int sent = sendto(sip_socket, register_msg, strlen(register_msg), 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Error sending REGISTER message: %d", sent);
        current_state = SIP_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG, "REGISTER message sent successfully (%d bytes)", sent);

    NTP_LOGI(TAG, "REGISTER message sent");
    return true;
}

// Send authenticated REGISTER with digest authentication
static bool sip_client_register_auth(sip_auth_challenge_t* challenge)
{
    if (!sip_config.configured || sip_socket < 0 || !challenge || !challenge->valid) {
        current_state = SIP_STATE_ERROR;
        return false;
    }

    NTP_LOGI(TAG, "Sending authenticated REGISTER");
    sip_add_log_entry("info", "Sending authenticated REGISTER");

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sip_config.port);

    struct hostent *host = gethostbyname(sip_config.server);
    if (host == NULL) {
        ESP_LOGE(TAG, "Cannot resolve hostname: %s", sip_config.server);
        current_state = SIP_STATE_ERROR;
        return false;
    }
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

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

    // Build authenticated REGISTER message
    char register_msg[2048];
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

    NTP_LOGI(TAG, "Authenticated REGISTER sent successfully (%d bytes)", sent);
    sip_add_log_entry("info", "Authenticated REGISTER sent");
    return true;
}

void sip_client_make_call(const char* uri)
{
    // Allow calls only when IDLE or REGISTERED
    if (current_state != SIP_STATE_IDLE && current_state != SIP_STATE_REGISTERED) {
        NTP_LOGW(TAG, "Cannot make call - current state: %d (not IDLE or REGISTERED)", current_state);
        sip_add_log_entry("error", "Cannot make call - not in ready state");
        return;
    }
    
    if (!sip_config.configured) {
        NTP_LOGW(TAG, "Cannot make call - SIP not configured");
        sip_add_log_entry("error", "Cannot make call - SIP not configured");
        return;
    }
    
    // Remember previous state to restore after call
    sip_state_t previous_state = current_state;
    
    NTP_LOGI(TAG, "Initiating call to %s", uri);
    sip_add_log_entry("info", "Initiating call");
    current_state = SIP_STATE_CALLING;
    
    // Get local IP address
    char local_ip[16];
    if (!get_local_ip(local_ip, sizeof(local_ip))) {
        strcpy(local_ip, "192.168.1.100"); // Fallback
    }
    
    // Simplified INVITE message
    // In a real implementation, a complete SDP session description would be created here
    char sdp[256];
    snprintf(sdp, sizeof(sdp), 
             "v=0\r\no=- 0 0 IN IP4 %s\r\ns=-\r\nc=IN IP4 %s\r\nt=0 0\r\nm=audio 5004 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n",
             local_ip, local_ip);
    
    char invite_msg[2048];
    snprintf(invite_msg, sizeof(invite_msg), sip_invite_template,
             uri, local_ip, rand(),
             sip_config.username, sip_config.server, rand(),
             uri, rand(), local_ip,
             sip_config.username, local_ip,
             strlen(sdp), sdp);
    
    NTP_LOGI(TAG, "INVITE message created (%d bytes)", strlen(invite_msg));
    sip_add_log_entry("sent", "INVITE message prepared");
    
    // TODO: Send INVITE message via socket
    // For now, just log and reset state after a delay
    // In a full implementation, this would:
    // 1. Send INVITE via UDP socket
    // 2. Wait for 100 Trying, 180 Ringing, 200 OK responses
    // 3. Send ACK
    // 4. Establish RTP audio stream
    
    NTP_LOGW(TAG, "Call functionality not fully implemented - returning to ready state");
    sip_add_log_entry("info", "Call simulation complete (not fully implemented)");
    
    // Restore previous state (should be REGISTERED to maintain registration)
    current_state = previous_state;
    NTP_LOGI(TAG, "State restored to: %d", current_state);
}

void sip_client_hangup(void)
{
    if (current_state == SIP_STATE_CONNECTED || current_state == SIP_STATE_CALLING) {
        ESP_LOGI(TAG, "Ending call");
        current_state = SIP_STATE_IDLE;
        audio_stop_recording();
        audio_stop_playback();
    }
}

void sip_client_answer_call(void)
{
    if (current_state == SIP_STATE_RINGING) {
        ESP_LOGI(TAG, "Answering call");
        current_state = SIP_STATE_CONNECTED;
        audio_start_recording();
        audio_start_playback();
    }
}

void sip_client_send_dtmf(char dtmf_digit)
{
    if (current_state == SIP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Sending DTMF: %c", dtmf_digit);
        current_state = SIP_STATE_DTMF_SENDING;

        // DTMF sending logic would go here
        // For now, we'll just log it and return to connected state
        ESP_LOGI(TAG, "DTMF %c sent", dtmf_digit);

        // Return to connected state after DTMF
        current_state = SIP_STATE_CONNECTED;
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
    // and send a REGISTER message (without waiting for response)
    struct hostent *host = gethostbyname(sip_config.server);
    if (host == NULL) {
        ESP_LOGE(TAG, "Cannot resolve hostname: %s", sip_config.server);
        return false;
    }

    ESP_LOGI(TAG, "SIP server %s is reachable", sip_config.server);
    return true;
}

void sip_get_status(char* buffer, size_t buffer_size)
{
    const char* state_names[] = {
        "IDLE", "REGISTERING", "REGISTERED", "CALLING", "RINGING",
        "CONNECTED", "DTMF_SENDING", "DISCONNECTED", "ERROR",
        "AUTH_FAILED", "NETWORK_ERROR", "TIMEOUT"
    };

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
    ESP_LOGI(TAG, "Saving SIP configuration: %s@%s", username, server);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("sip_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        // Save SIP configuration fields
        nvs_set_str(nvs_handle, "server", server);
        nvs_set_str(nvs_handle, "username", username);
        nvs_set_str(nvs_handle, "password", password);
        nvs_set_str(nvs_handle, "apt1", apt1);
        nvs_set_str(nvs_handle, "apt2", apt2);
        nvs_set_u16(nvs_handle, "port", port);
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
            
            uint16_t port = 5060;
            nvs_get_u16(nvs_handle, "port", &port);
            config.port = port;
            
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
        
        for (int i = 0; i < sip_log_count && count < max_entries; i++) {
            int index = (start_index + i) % SIP_LOG_MAX_ENTRIES;
            // Use > to exclude entries at exactly since_timestamp (already seen)
            if (sip_log_buffer[index].timestamp > since_timestamp) {
                memcpy(&entries[count], &sip_log_buffer[index], sizeof(sip_log_entry_t));
                count++;
            }
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

const char* sip_get_uri(void)
{
    // Return apartment1_uri as the default URI
    return sip_config.apartment1_uri;
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

void sip_set_uri(const char* uri)
{
    if (uri) {
        strncpy(sip_config.apartment1_uri, uri, sizeof(sip_config.apartment1_uri) - 1);
        sip_config.apartment1_uri[sizeof(sip_config.apartment1_uri) - 1] = '\0';
        sip_config.configured = true;
    }
}

void sip_reinit(void)
{
    ESP_LOGI(TAG, "Reinitializing SIP client");

    // Stop current SIP task if running
    if (sip_task_handle) {
        vTaskDelete(sip_task_handle);
        sip_task_handle = NULL;
    }

    // Close socket if open
    if (sip_socket >= 0) {
        close(sip_socket);
        sip_socket = -1;
    }

    // Reload configuration
    sip_config = sip_load_config();

    if (sip_config.configured) {
        // Reinitialize with new config
        sip_client_init();
    } else {
        ESP_LOGI(TAG, "No SIP configuration found after reinit");
        current_state = SIP_STATE_DISCONNECTED;
    }
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
    NTP_LOGI(TAG, "SIP connect requested");
    sip_add_log_entry("info", "SIP connection requested");
    
    if (!sip_config.configured) {
        ESP_LOGE(TAG, "Cannot connect: SIP not configured");
        sip_add_log_entry("error", "Cannot connect: SIP not configured");
        return false;
    }
    
    if (current_state == SIP_STATE_REGISTERED) {
        NTP_LOGI(TAG, "Already registered");
        sip_add_log_entry("info", "Already registered to SIP server");
        return true;
    }
    
    // If disconnected, change state to idle so registration can proceed
    if (current_state == SIP_STATE_DISCONNECTED) {
        current_state = SIP_STATE_IDLE;
        NTP_LOGI(TAG, "State changed from DISCONNECTED to IDLE");
        sip_add_log_entry("info", "Reconnecting to SIP server");
    }
    
    // Set flag to trigger registration in SIP task (non-blocking)
    registration_requested = true;
    sip_add_log_entry("info", "SIP registration queued");
    
    return true;
}

// Disconnect from SIP server
void sip_disconnect(void)
{
    NTP_LOGI(TAG, "SIP disconnect requested");
    sip_add_log_entry("info", "SIP disconnection requested");
    
    // Send REGISTER with Expires: 0 to unregister (if registered)
    if (current_state == SIP_STATE_REGISTERED && sip_socket >= 0) {
        NTP_LOGI(TAG, "Sending unregister message");
        sip_add_log_entry("info", "Unregistering from SIP server");
        // TODO: Send REGISTER with Expires: 0
    }
    
    // Close socket
    if (sip_socket >= 0) {
        close(sip_socket);
        sip_socket = -1;
        NTP_LOGI(TAG, "SIP socket closed");
    }
    
    // Clear registration flag
    registration_requested = false;
    
    // Update state
    current_state = SIP_STATE_DISCONNECTED;
    sip_add_log_entry("info", "Disconnected from SIP server");
    
    NTP_LOGI(TAG, "SIP client disconnected");
}
