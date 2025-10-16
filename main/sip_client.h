#ifndef SIP_CLIENT_H
#define SIP_CLIENT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char server[64];
    char username[32];
    char password[32];
    char apartment1_uri[64];
    char apartment2_uri[64];
    int port;
    bool configured;
} sip_config_t;

typedef enum {
    SIP_STATE_IDLE,
    SIP_STATE_REGISTERING,
    SIP_STATE_REGISTERED,
    SIP_STATE_CALLING,
    SIP_STATE_RINGING,
    SIP_STATE_CONNECTED,
    SIP_STATE_DTMF_SENDING,
    SIP_STATE_DISCONNECTED,
    SIP_STATE_ERROR,
    SIP_STATE_AUTH_FAILED,
    SIP_STATE_NETWORK_ERROR,
    SIP_STATE_TIMEOUT
} sip_state_t;

void sip_client_init(void);
void sip_client_deinit(void);
bool sip_client_register(void);
void sip_client_make_call(const char* uri);
void sip_client_hangup(void);
void sip_client_answer_call(void);
void sip_client_send_dtmf(char dtmf_digit);
sip_state_t sip_client_get_state(void);
bool sip_client_test_connection(void);
void sip_get_status(char* buffer, size_t buffer_size);
void sip_save_config(const char* server, const char* username, const char* password,
                     const char* apt1, const char* apt2, int port);
sip_config_t sip_load_config(void);

// Additional getter/setter functions for web interface
bool sip_is_registered(void);
const char* sip_get_server(void);
const char* sip_get_username(void);
const char* sip_get_password(void);
const char* sip_get_target1(void);  // Returns apartment1_uri
const char* sip_get_target2(void);  // Returns apartment2_uri
void sip_set_server(const char* server);
void sip_set_username(const char* username);
void sip_set_password(const char* password);
void sip_set_target1(const char* target);
void sip_set_target2(const char* target);
void sip_reinit(void);
bool sip_test_configuration(void);

// Log management
typedef struct {
    uint64_t timestamp;  // Changed to uint64_t to support NTP timestamps
    char type[16];
    char message[256];
} sip_log_entry_t;

int sip_get_log_entries(sip_log_entry_t* entries, int max_entries, uint64_t since_timestamp);

// Connection management
bool sip_connect(void);
void sip_disconnect(void);

#endif