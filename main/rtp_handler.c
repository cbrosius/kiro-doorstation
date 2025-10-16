#include "rtp_handler.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RTP";

// RTP session state
static int rtp_socket = -1;
static struct sockaddr_in remote_addr;
static uint16_t sequence_number = 0;
static uint32_t timestamp = 0;
static uint32_t ssrc = 0;
static bool session_active = false;

// Telephone-event callback
static telephone_event_callback_t telephone_event_callback = NULL;

// Track last processed telephone-event timestamp for deduplication
static uint32_t last_telephone_event_timestamp = 0;

// Forward declarations
static void rtp_process_telephone_event(const rtp_header_t* header, const uint8_t* payload, size_t payload_size);
static char rtp_map_event_to_char(uint8_t event_code);

// G.711 μ-law encoding table (simplified)
static const int16_t mulaw_decode_table[256] = {
    -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
    -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
    -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
    -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
     -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
     -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
     -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
     -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
     -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
     -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
      -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
      -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
      -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
      -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
      -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
       -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
     32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
     23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
     15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
     11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
      7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
      5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
      3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
      2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
      1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
      1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
       876,   844,   812,   780,   748,   716,   684,   652,
       620,   588,   556,   524,   492,   460,   428,   396,
       372,   356,   340,   324,   308,   292,   276,   260,
       244,   228,   212,   196,   180,   164,   148,   132,
       120,   112,   104,    96,    88,    80,    72,    64,
        56,    48,    40,    32,    24,    16,     8,     0
};

// Simple μ-law encoder
static uint8_t linear_to_mulaw(int16_t sample)
{
    const uint16_t MULAW_MAX = 0x1FFF;
    const uint16_t MULAW_BIAS = 33;
    uint16_t mask = 0x1000;
    uint8_t sign = (sample < 0) ? 0x80 : 0;
    uint8_t position = 12;
    uint8_t lsb = 0;
    
    if (sign)
        sample = -sample;
    
    sample += MULAW_BIAS;
    if (sample > MULAW_MAX)
        sample = MULAW_MAX;
    
    for (; ((sample & mask) != mask && position >= 5); mask >>= 1, position--);
    
    lsb = (sample >> (position - 4)) & 0x0f;
    return (~(sign | ((position - 5) << 4) | lsb));
}

void rtp_init(void)
{
    ESP_LOGI(TAG, "RTP handler initialized");
    ssrc = esp_random(); // Random SSRC
    sequence_number = esp_random() & 0xFFFF;
    timestamp = esp_random();
}

bool rtp_start_session(const char* remote_ip, uint16_t remote_port, uint16_t local_port)
{
    if (session_active) {
        ESP_LOGW(TAG, "RTP session already active");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting RTP session: %s:%d (local port: %d)", remote_ip, remote_port, local_port);
    
    // Create UDP socket
    rtp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rtp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create RTP socket");
        return false;
    }
    
    // Bind to local port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);
    
    if (bind(rtp_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind RTP socket to port %d", local_port);
        close(rtp_socket);
        rtp_socket = -1;
        return false;
    }
    
    // Set remote address
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    
    // Convert IP address
    if (inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "Invalid remote IP address: %s", remote_ip);
        close(rtp_socket);
        rtp_socket = -1;
        return false;
    }
    
    // Set socket to non-blocking mode
    int flags = fcntl(rtp_socket, F_GETFL, 0);
    fcntl(rtp_socket, F_SETFL, flags | O_NONBLOCK);
    
    session_active = true;
    ESP_LOGI(TAG, "RTP session started successfully");
    return true;
}

void rtp_stop_session(void)
{
    if (!session_active) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping RTP session");
    
    if (rtp_socket >= 0) {
        close(rtp_socket);
        rtp_socket = -1;
    }
    
    session_active = false;
}

int rtp_send_audio(const int16_t* samples, size_t sample_count)
{
    if (!session_active || rtp_socket < 0) {
        return -1;
    }
    
    // Allocate buffer for RTP packet (header + payload)
    size_t packet_size = sizeof(rtp_header_t) + sample_count;
    uint8_t* packet = malloc(packet_size);
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate RTP packet buffer");
        return -1;
    }
    
    // Build RTP header
    rtp_header_t* header = (rtp_header_t*)packet;
    header->version = 2;
    header->padding = 0;
    header->extension = 0;
    header->csrc_count = 0;
    header->marker = 0;
    header->payload_type = 0; // PCMU (G.711 μ-law)
    header->sequence = htons(sequence_number++);
    header->timestamp = htonl(timestamp);
    header->ssrc = htonl(ssrc);
    
    // Encode audio samples to μ-law
    uint8_t* payload = packet + sizeof(rtp_header_t);
    for (size_t i = 0; i < sample_count; i++) {
        payload[i] = linear_to_mulaw(samples[i]);
    }
    
    // Send packet
    int sent = sendto(rtp_socket, packet, packet_size, 0,
                     (struct sockaddr*)&remote_addr, sizeof(remote_addr));
    
    free(packet);
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send RTP packet");
        return -1;
    }
    
    // Update timestamp (8000 Hz sample rate)
    timestamp += sample_count;
    
    return sent;
}

int rtp_receive_audio(int16_t* samples, size_t max_samples)
{
    if (!session_active || rtp_socket < 0) {
        return -1;
    }
    
    uint8_t buffer[1500]; // MTU size
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    int received = recvfrom(rtp_socket, buffer, sizeof(buffer), MSG_DONTWAIT,
                           (struct sockaddr*)&from_addr, &from_len);
    
    if (received <= 0) {
        return 0; // No data available
    }
    
    if (received < sizeof(rtp_header_t)) {
        ESP_LOGW(TAG, "Received packet too small");
        return 0;
    }
    
    // Parse RTP header
    const rtp_header_t* header = (const rtp_header_t*)buffer;
    const uint8_t* payload = buffer + sizeof(rtp_header_t);
    size_t payload_size = received - sizeof(rtp_header_t);
    
    // Extract payload type from RTP header
    uint8_t payload_type = header->payload_type;
    
    // Route by payload type
    if (payload_type == 101) {
        // RFC 4733 telephone-event
        rtp_process_telephone_event(header, payload, payload_size);
        return 0; // No audio samples to return
    } else if (payload_type == 0 || payload_type == 8) {
        // Audio payload (PCMU=0, PCMA=8)
        // Decode μ-law to linear PCM
        size_t sample_count = (payload_size < max_samples) ? payload_size : max_samples;
        for (size_t i = 0; i < sample_count; i++) {
            samples[i] = mulaw_decode_table[payload[i]];
        }
        return sample_count;
    } else {
        // Unknown payload type
        ESP_LOGW(TAG, "Unknown RTP payload type: %d", payload_type);
        return 0;
    }
}

bool rtp_is_active(void)
{
    return session_active;
}

void rtp_set_telephone_event_callback(telephone_event_callback_t callback)
{
    telephone_event_callback = callback;
    ESP_LOGI(TAG, "Telephone-event callback %s", callback ? "registered" : "unregistered");
}

// Map RFC 4733 event code to DTMF character
static char rtp_map_event_to_char(uint8_t event_code)
{
    // RFC 4733 event codes 0-15 map to DTMF digits
    switch (event_code) {
        case DTMF_EVENT_0: return '0';
        case DTMF_EVENT_1: return '1';
        case DTMF_EVENT_2: return '2';
        case DTMF_EVENT_3: return '3';
        case DTMF_EVENT_4: return '4';
        case DTMF_EVENT_5: return '5';
        case DTMF_EVENT_6: return '6';
        case DTMF_EVENT_7: return '7';
        case DTMF_EVENT_8: return '8';
        case DTMF_EVENT_9: return '9';
        case DTMF_EVENT_STAR: return '*';
        case DTMF_EVENT_HASH: return '#';
        case DTMF_EVENT_A: return 'A';
        case DTMF_EVENT_B: return 'B';
        case DTMF_EVENT_C: return 'C';
        case DTMF_EVENT_D: return 'D';
        default: return '\0'; // Invalid event code
    }
}

// Internal: Parse and process telephone-event packet
static void rtp_process_telephone_event(const rtp_header_t* header, const uint8_t* payload, size_t payload_size)
{
    // Check packet size before parsing (minimum 4 bytes for RFC 4733)
    if (payload_size < sizeof(rtp_telephone_event_t)) {
        ESP_LOGE(TAG, "Malformed telephone-event: packet too small (%d bytes, expected %d)", 
                 payload_size, sizeof(rtp_telephone_event_t));
        return; // Continue processing without crashing
    }
    
    // Validate header pointer
    if (header == NULL || payload == NULL) {
        ESP_LOGE(TAG, "Malformed telephone-event: NULL pointer (header=%p, payload=%p)", 
                 header, payload);
        return; // Continue processing without crashing
    }
    
    // Parse telephone-event structure
    const rtp_telephone_event_t* event = (const rtp_telephone_event_t*)payload;
    
    // Validate event code is within valid range (0-15 for DTMF)
    if (event->event > DTMF_EVENT_D) {
        ESP_LOGE(TAG, "Malformed telephone-event: invalid event code %d (valid range: 0-15)", 
                 event->event);
        return; // Continue processing without crashing
    }
    
    // Extract end bit from e_r_volume field (bit 7)
    bool end_bit = (event->e_r_volume & 0x80) != 0;
    uint8_t volume = event->e_r_volume & 0x3F; // Lower 6 bits
    uint16_t duration = ntohs(event->duration);
    
    // Get RTP timestamp
    uint32_t rtp_timestamp = ntohl(header->timestamp);
    
    ESP_LOGD(TAG, "Telephone-event: code=%d, end=%d, volume=%d, duration=%d, ts=%u", 
             event->event, end_bit, volume, duration, rtp_timestamp);
    
    // Only process when end bit is set and timestamp is new (deduplication)
    if (end_bit && rtp_timestamp != last_telephone_event_timestamp) {
        last_telephone_event_timestamp = rtp_timestamp;
        
        // Map event code to DTMF character
        char dtmf_char = rtp_map_event_to_char(event->event);
        
        if (dtmf_char != '\0') {
            ESP_LOGI(TAG, "DTMF detected: '%c' (event code %d)", dtmf_char, event->event);
            
            // Invoke callback if registered
            if (telephone_event_callback) {
                telephone_event_callback(event->event);
            }
        } else {
            ESP_LOGE(TAG, "Malformed telephone-event: failed to map event code %d to character", 
                     event->event);
        }
    }
}
