#ifndef SIP_CLIENT_H
#define SIP_CLIENT_H

#include <stdbool.h>

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
    SIP_STATE_CALLING,
    SIP_STATE_RINGING,
    SIP_STATE_CONNECTED,
    SIP_STATE_DTMF_SENDING,
    SIP_STATE_DISCONNECTED,
    SIP_STATE_ERROR
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

#endif