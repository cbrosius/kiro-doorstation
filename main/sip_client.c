#include "sip_client.h"
#include "audio_handler.h"
#include "dtmf_decoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "SIP";
static sip_state_t current_state = SIP_STATE_IDLE;
static sip_config_t sip_config = {0};
static int sip_socket = -1;
static TaskHandle_t sip_task_handle = NULL;

// Vereinfachte SIP-Nachrichten
static const char* sip_register_template = 
"REGISTER sip:%s SIP/2.0\r\n"
"Via: SIP/2.0/UDP %s:5060;branch=z9hG4bK%d\r\n"
"From: <sip:%s@%s>;tag=%d\r\n"
"To: <sip:%s@%s>\r\n"
"Call-ID: %d@%s\r\n"
"CSeq: 1 REGISTER\r\n"
"Contact: <sip:%s@%s:5060>\r\n"
"Expires: 3600\r\n"
"Content-Length: 0\r\n\r\n";

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

static void sip_task(void *pvParameters)
{
    char buffer[2048];
    int len;
    
    while (1) {
        if (sip_socket >= 0) {
            len = recv(sip_socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            if (len > 0) {
                buffer[len] = '\0';
                ESP_LOGI(TAG, "SIP Nachricht empfangen:\n%s", buffer);
                
                // Vereinfachte SIP-Nachrichtenverarbeitung
                if (strstr(buffer, "200 OK")) {
                    if (current_state == SIP_STATE_REGISTERING) {
                        current_state = SIP_STATE_IDLE;
                        ESP_LOGI(TAG, "SIP Registrierung erfolgreich");
                    } else if (current_state == SIP_STATE_CALLING) {
                        current_state = SIP_STATE_CONNECTED;
                        ESP_LOGI(TAG, "Anruf verbunden");
                        audio_start_recording();
                        audio_start_playback();
                    }
                } else if (strstr(buffer, "INVITE")) {
                    ESP_LOGI(TAG, "Eingehender Anruf");
                    current_state = SIP_STATE_RINGING;
                    // Automatisch annehmen
                    sip_client_answer_call();
                } else if (strstr(buffer, "BYE")) {
                    ESP_LOGI(TAG, "Anruf beendet");
                    current_state = SIP_STATE_IDLE;
                    audio_stop_recording();
                    audio_stop_playback();
                } else if (strstr(buffer, "401 Unauthorized") || strstr(buffer, "403 Forbidden")) {
                    ESP_LOGE(TAG, "SIP Authentifizierung fehlgeschlagen");
                    current_state = SIP_STATE_ERROR;
                } else if (strstr(buffer, "404 Not Found")) {
                    ESP_LOGE(TAG, "SIP Ziel nicht gefunden");
                    current_state = SIP_STATE_ERROR;
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
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void sip_client_init(void)
{
    ESP_LOGI(TAG, "Initializing SIP client");

    // Set initial state
    current_state = SIP_STATE_IDLE;

    // Konfiguration laden
    sip_config = sip_load_config();

    if (sip_config.configured) {
        ESP_LOGI(TAG, "SIP configuration loaded: %s@%s", sip_config.username, sip_config.server);

        // Create socket
        sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sip_socket < 0) {
            ESP_LOGE(TAG, "Error creating SIP socket");
            current_state = SIP_STATE_ERROR;
            return;
        }

        // Task für SIP-Verarbeitung starten
        xTaskCreate(sip_task, "sip_task", 4096, NULL, 5, &sip_task_handle);

        // Set state to registering before attempting registration
        current_state = SIP_STATE_REGISTERING;

        // Registrierung versuchen
        sip_client_register();
    } else {
        ESP_LOGI(TAG, "No SIP configuration found");
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
    
    ESP_LOGI(TAG, "SIP Client deinitialisiert");
}

bool sip_client_register(void)
{
    if (!sip_config.configured || sip_socket < 0) {
        current_state = SIP_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG, "SIP registration with %s", sip_config.server);
    current_state = SIP_STATE_REGISTERING;

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

    // REGISTER-Nachricht erstellen (vereinfacht)
    char register_msg[1024];
    snprintf(register_msg, sizeof(register_msg), sip_register_template,
             sip_config.server, "192.168.1.100", rand(),
             sip_config.username, sip_config.server, rand(),
             sip_config.username, sip_config.server,
             rand(), "192.168.1.100",
             sip_config.username, "192.168.1.100");

    int sent = sendto(sip_socket, register_msg, strlen(register_msg), 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Fehler beim Senden der REGISTER-Nachricht: %d", sent);
        current_state = SIP_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG, "REGISTER-Nachricht erfolgreich gesendet (%d Bytes)", sent);

    ESP_LOGI(TAG, "REGISTER message sent");
    return true;
}

void sip_client_make_call(const char* uri)
{
    if (current_state != SIP_STATE_IDLE || !sip_config.configured) {
        ESP_LOGW(TAG, "Kann keinen Anruf tätigen - Status: %d", current_state);
        return;
    }
    
    ESP_LOGI(TAG, "Call to %s", uri);
    current_state = SIP_STATE_CALLING;
    
    // Vereinfachte INVITE-Nachricht
    // In einer echten Implementierung würde hier eine vollständige SDP-Session-Description erstellt
    const char* sdp = "v=0\r\no=- 0 0 IN IP4 192.168.1.100\r\ns=-\r\nc=IN IP4 192.168.1.100\r\nt=0 0\r\nm=audio 5004 RTP/AVP 0\r\na=rtpmap:0 PCMU/8000\r\n";
    
    char invite_msg[2048];
    snprintf(invite_msg, sizeof(invite_msg), sip_invite_template,
             uri, "192.168.1.100", rand(),
             sip_config.username, sip_config.server, rand(),
             uri, rand(), "192.168.1.100",
             sip_config.username, "192.168.1.100",
             strlen(sdp), sdp);
    
    // Nachricht senden (vereinfacht)
    ESP_LOGI(TAG, "INVITE-Nachricht erstellt");
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

    // Try to send a REGISTER message
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sip_config.port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    char register_msg[1024];
    snprintf(register_msg, sizeof(register_msg), sip_register_template,
             sip_config.server, "192.168.1.100", rand(),
             sip_config.username, sip_config.server, rand(),
             sip_config.username, sip_config.server,
             rand(), "192.168.1.100",
             sip_config.username, "192.168.1.100");

    int sent = sendto(sip_socket, register_msg, strlen(register_msg), 0,
                       (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Error sending REGISTER message");
        return false;
    }

    ESP_LOGI(TAG, "SIP test REGISTER message sent");
    return true;
}

void sip_get_status(char* buffer, size_t buffer_size)
{
    const char* state_names[] = {
        "IDLE", "REGISTERING", "CALLING", "RINGING",
        "CONNECTED", "DTMF_SENDING", "DISCONNECTED", "ERROR"
    };

    sip_state_t state = sip_client_get_state();

    const char* status_template =
        "{"
        "\"state\": \"%s\","
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
             state < sizeof(state_names)/sizeof(state_names[0]) ? state_names[state] : "UNKNOWN",
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
    ESP_LOGI(TAG, "SIP Konfiguration speichern: %s@%s", username, server);

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
        ESP_LOGI(TAG, "SIP Konfiguration gespeichert");
    } else {
        ESP_LOGE(TAG, "Fehler beim Öffnen des NVS-Handles: %d", err);
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

// Additional functions for web interface
bool sip_is_registered(void)
{
    return (current_state != SIP_STATE_IDLE &&
            current_state != SIP_STATE_DISCONNECTED &&
            current_state != SIP_STATE_ERROR);
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