#ifndef SIP_AUTH_H
#define SIP_AUTH_H

#include <stdbool.h>

// SIP authentication challenge structure
typedef struct {
  char realm[128];
  char nonce[128];
  char qop[32];
  char opaque[128];
  char algorithm[32];
  bool valid;
} sip_auth_challenge_t;

// Parse WWW-Authenticate header
sip_auth_challenge_t parse_www_authenticate(const char *buffer);

// Calculate digest authentication response
void calculate_digest_response(const char *username, const char *password,
                               const char *realm, const char *nonce,
                               const char *method, const char *uri,
                               const char *qop, const char *nc,
                               const char *cnonce, char *response_out);

#endif // SIP_AUTH_H
