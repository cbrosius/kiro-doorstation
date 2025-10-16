#ifndef DTMF_DECODER_H
#define DTMF_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DTMF_0 = '0',
    DTMF_1 = '1',
    DTMF_2 = '2',
    DTMF_3 = '3',
    DTMF_4 = '4',
    DTMF_5 = '5',
    DTMF_6 = '6',
    DTMF_7 = '7',
    DTMF_8 = '8',
    DTMF_9 = '9',
    DTMF_STAR = '*',
    DTMF_HASH = '#',
    DTMF_A = 'A',
    DTMF_B = 'B',
    DTMF_C = 'C',
    DTMF_D = 'D',
    DTMF_NONE = 0
} dtmf_tone_t;

// Security configuration structure
typedef struct {
    bool pin_enabled;           // PIN protection enabled
    char pin_code[9];           // PIN code (max 8 digits + null)
    uint32_t timeout_ms;        // Command timeout in milliseconds
    uint8_t max_attempts;       // Max failed attempts per call
} dtmf_security_config_t;

// Command state tracking structure
typedef struct {
    char buffer[16];            // Command buffer
    uint8_t buffer_index;       // Current position in buffer
    uint32_t start_time_ms;     // Command start timestamp
    uint8_t failed_attempts;    // Failed attempts this call
    bool rate_limited;          // Rate limit triggered
    uint32_t last_event_ts;     // Last RTP timestamp processed
} dtmf_command_state_t;

// Command type enumeration
typedef enum {
    CMD_DOOR_OPEN,      // *[PIN]# or *1# (legacy)
    CMD_LIGHT_TOGGLE,   // *2#
    CMD_CONFIG_CHANGE,  // Security configuration changed
    CMD_INVALID
} dtmf_command_type_t;

// Security log entry structure
typedef struct {
    uint64_t timestamp;         // NTP timestamp in milliseconds
    dtmf_command_type_t type;   // Command type
    bool success;               // Execution success
    char command[16];           // Full command string
    char caller_id[64];         // SIP caller identifier
    char reason[32];            // Failure reason (if applicable)
} dtmf_security_log_t;

typedef void (*dtmf_callback_t)(dtmf_tone_t tone);

void dtmf_decoder_init(void);
void dtmf_set_callback(dtmf_callback_t callback);

// RFC 4733 telephone-event processing functions
void dtmf_process_telephone_event(uint8_t event);
void dtmf_load_security_config(void);
void dtmf_save_security_config(const dtmf_security_config_t* config);
void dtmf_get_security_config(dtmf_security_config_t* config);
void dtmf_reset_call_state(void);

// Security log retrieval
int dtmf_get_security_logs(dtmf_security_log_t* entries, int max_entries, uint64_t since_timestamp);

#endif