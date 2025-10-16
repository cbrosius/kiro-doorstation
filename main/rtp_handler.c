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
    
    // Parse RTP header (skip header validation for now)
    uint8_t* payload = buffer + sizeof(rtp_header_t);
    size_t payload_size = received - sizeof(rtp_header_t);
    
    // Decode μ-law to linear PCM
    size_t sample_count = (payload_size < max_samples) ? payload_size : max_samples;
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = mulaw_decode_table[payload[i]];
    }
    
    return sample_count;
}

bool rtp_is_active(void)
{
    return session_active;
}
