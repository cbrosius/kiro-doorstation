#ifndef RTP_HANDLER_H
#define RTP_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// RTP header structure
typedef struct {
    uint8_t version:2;      // Version (2)
    uint8_t padding:1;      // Padding flag
    uint8_t extension:1;    // Extension flag
    uint8_t csrc_count:4;   // CSRC count
    uint8_t marker:1;       // Marker bit
    uint8_t payload_type:7; // Payload type
    uint16_t sequence;      // Sequence number
    uint32_t timestamp;     // Timestamp
    uint32_t ssrc;          // Synchronization source
} __attribute__((packed)) rtp_header_t;

// Initialize RTP handler
void rtp_init(void);

// Start RTP session
bool rtp_start_session(const char* remote_ip, uint16_t remote_port, uint16_t local_port);

// Stop RTP session
void rtp_stop_session(void);

// Send audio data via RTP
int rtp_send_audio(const int16_t* samples, size_t sample_count);

// Receive audio data via RTP
int rtp_receive_audio(int16_t* samples, size_t max_samples);

// Check if RTP session is active
bool rtp_is_active(void);

#endif // RTP_HANDLER_H
