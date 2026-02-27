#ifndef SIP_MSG_H
#define SIP_MSG_H

#include <stdbool.h>

// SIP request headers structure for parsing
typedef struct {
  char call_id[128];
  char via_header[256];
  char from_header[256];
  char to_header[256];
  char contact[128];
  int cseq_num;
  char cseq_method[32];
  bool valid;
} sip_request_headers_t;

// Extract common SIP headers from a request message
sip_request_headers_t extract_request_headers(const char *buffer);

// Generic SIP response builder and sender
void send_sip_response(int code, const char *reason,
                       const sip_request_headers_t *req_headers,
                       const char *extra_headers, const char *body);

// Send ACK for error responses to INVITE (RFC 3261 §17.1.1.3)
void send_ack_for_error_response(const char *response_buffer);

#endif // SIP_MSG_H
