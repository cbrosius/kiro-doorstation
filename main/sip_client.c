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
    ESP_LOGI(TAG, "SIP Client initialisieren");

    // Set initial state
    current_state = SIP_STATE_IDLE;

    // Konfiguration laden
    sip_config = sip_load_config();

    if (sip_config.configured) {
        ESP_LOGI(TAG, "SIP Konfiguration geladen: %s@%s", sip_config.username, sip_config.server);

        // Socket erstellen
        sip_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sip_socket < 0) {
            ESP_LOGE(TAG, "Fehler beim Erstellen des SIP Sockets");
            current_state = SIP_STATE_ERROR;
            return;
        }

        // Task für SIP-Verarbeitung starten
        xTaskCreate(sip_task, "sip_task", 4096, NULL, 5, &sip_task_handle);

        // Registrierung versuchen
        sip_client_register();
    } else {
        ESP_LOGI(TAG, "Keine SIP Konfiguration gefunden");
        current_state = SIP_STATE_DISCONNECTED;
    }

    ESP_LOGI(TAG, "SIP Client initialisiert");
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

    ESP_LOGI(TAG, "SIP Registrierung bei %s", sip_config.server);
    current_state = SIP_STATE_REGISTERING;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(sip_config.port);

    struct hostent *host = gethostbyname(sip_config.server);
    if (host == NULL) {
        ESP_LOGE(TAG, "Hostname nicht auflösbar: %s", sip_config.server);
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
        ESP_LOGE(TAG, "Fehler beim Senden der REGISTER-Nachricht");
        current_state = SIP_STATE_ERROR;
        return false;
    }

    ESP_LOGI(TAG, "REGISTER-Nachricht gesendet");
    return true;
}

void sip_client_make_call(const char* uri)
{
    if (current_state != SIP_STATE_IDLE || !sip_config.configured) {
        ESP_LOGW(TAG, "Kann keinen Anruf tätigen - Status: %d", current_state);
        return;
    }
    
    ESP_LOGI(TAG, "Anruf an %s", uri);
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
        ESP_LOGI(TAG, "Anruf beenden");
        current_state = SIP_STATE_IDLE;
        audio_stop_recording();
        audio_stop_playback();
    }
}

void sip_client_answer_call(void)
{
    if (current_state == SIP_STATE_RINGING) {
        ESP_LOGI(TAG, "Anruf annehmen");
        current_state = SIP_STATE_CONNECTED;
        audio_start_recording();
        audio_start_playback();
    }
}

void sip_client_send_dtmf(char dtmf_digit)
{
    if (current_state == SIP_STATE_CONNECTED) {
        ESP_LOGI(TAG, "Sende DTMF: %c", dtmf_digit);
        current_state = SIP_STATE_DTMF_SENDING;

        // DTMF sending logic would go here
        // For now, we'll just log it and return to connected state
        ESP_LOGI(TAG, "DTMF %c gesendet", dtmf_digit);

        // Return to connected state after DTMF
        current_state = SIP_STATE_CONNECTED;
    } else {
        ESP_LOGW(TAG, "Kann DTMF nicht senden - Status: %d", current_state);
    }
}

bool sip_client_test_connection(void)
{
    ESP_LOGI(TAG, "SIP Verbindung testen");

    if (!sip_config.configured) {
        ESP_LOGE(TAG, "Keine SIP Konfiguration verfügbar");
        current_state = SIP_STATE_ERROR;
        return false;
    }

    if (sip_socket < 0) {
        ESP_LOGE(TAG, "SIP Socket nicht verfügbar");
        current_state = SIP_STATE_ERROR;
        return false;
    }

    // Temporarily store current state
    sip_state_t original_state = current_state;

    // Attempt registration
    bool result = sip_client_register();

    // Wait a bit for response (in real implementation, this would be async)
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Check if we're in a success state
    if (current_state == SIP_STATE_IDLE) {
        ESP_LOGI(TAG, "SIP Verbindung erfolgreich");
        result = true;
    } else if (current_state == SIP_STATE_ERROR) {
        ESP_LOGE(TAG, "SIP Verbindung fehlgeschlagen");
        result = false;
    } else {
        ESP_LOGW(TAG, "SIP Verbindung noch in Bearbeitung");
        // Restore original state if test didn't complete
        current_state = original_state;
    }

    return result;
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

    snprintf(buffer, buffer_size, status_template,
             state < sizeof(state_names)/sizeof(state_names[0]) ? state_names[state] : "UNKNOWN",
             state,
             sip_config.configured ? "true" : "false",
             sip_config.server,
             sip_config.username,
             sip_config.apartment1_uri,
             sip_config.apartment2_uri,
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