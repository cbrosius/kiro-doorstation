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

// RFC 4733 telephone-event packet structure
typedef struct {
    uint8_t event;        // Event code (0-15 for DTMF)
    uint8_t e_r_volume;   // E(1bit) R(1bit) Volume(6bits)
    uint16_t duration;    // Duration in timestamp units
} __attribute__((packed)) rtp_telephone_event_t;

// DTMF event codes (RFC 4733 Section 3.2)
#define DTMF_EVENT_0    0
#define DTMF_EVENT_1    1
#define DTMF_EVENT_2    2
#define DTMF_EVENT_3    3
#define DTMF_EVENT_4    4
#define DTMF_EVENT_5    5
#define DTMF_EVENT_6    6
#define DTMF_EVENT_7    7
#define DTMF_EVENT_8    8
#define DTMF_EVENT_9    9
#define DTMF_EVENT_STAR 10  // *
#define DTMF_EVENT_HASH 11  // #
#define DTMF_EVENT_A    12
#define DTMF_EVENT_B    13
#define DTMF_EVENT_C    14
#define DTMF_EVENT_D    15

// Callback function pointer type for telephone-events
typedef void (*telephone_event_callback_t)(uint8_t event);

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

// Register callback for telephone-events
void rtp_set_telephone_event_callback(telephone_event_callback_t callback);

#endif // RTP_HANDLER_H
