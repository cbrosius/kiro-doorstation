#include "sip_auth.h"
#include "sip_client.h"
#include "sip_utils.h"
#include <stdio.h>
#include <string.h>


// Parse WWW-Authenticate header
sip_auth_challenge_t parse_www_authenticate(const char *buffer) {
  sip_auth_challenge_t challenge = {0};

  const char *auth_header = strstr(buffer, "WWW-Authenticate:");
  if (!auth_header) {
    return challenge;
  }

  // Extract realm
  extract_quoted_value(auth_header, "realm=\"", challenge.realm,
                       sizeof(challenge.realm));

  // Extract nonce
  extract_quoted_value(auth_header, "nonce=\"", challenge.nonce,
                       sizeof(challenge.nonce));

  // Extract qop
  extract_quoted_value(auth_header, "qop=\"", challenge.qop,
                       sizeof(challenge.qop));

  // Extract opaque (optional)
  extract_quoted_value(auth_header, "opaque=\"", challenge.opaque,
                       sizeof(challenge.opaque));

  // Extract algorithm (optional, defaults to MD5)
  const char *algo_start = strstr(auth_header, "algorithm=");
  if (algo_start) {
    algo_start += 10;
    if (*algo_start == '"') {
      algo_start++;
    }
    const char *algo_end = strpbrk(algo_start, "\",\r\n ");
    if (algo_end) {
      size_t algo_len = algo_end - algo_start;
      if (algo_len < sizeof(challenge.algorithm)) {
        strncpy(challenge.algorithm, algo_start, algo_len);
        challenge.algorithm[algo_len] = '\0';
      }
    }
  } else {
    strcpy(challenge.algorithm, "MD5");
  }

  challenge.valid =
      (strlen(challenge.realm) > 0 && strlen(challenge.nonce) > 0);

  if (challenge.valid) {
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg),
             "Auth challenge parsed: realm=%s, qop=%s, algorithm=%s",
             challenge.realm, challenge.qop, challenge.algorithm);
    sip_add_log_entry("info", log_msg);
  }

  return challenge;
}

// Calculate digest authentication response
void calculate_digest_response(const char *username, const char *password,
                               const char *realm, const char *nonce,
                               const char *method, const char *uri,
                               const char *qop, const char *nc,
                               const char *cnonce, char *response_out) {
  char ha1[33];
  char ha2[33];
  // Use static to avoid stack allocation
  // Thread-safe: only one SIP task accesses this function
  static char ha1_input[256];
  static char ha2_input[256];
  static char response_input[512];

  // Simplified logging to avoid format-truncation warnings with long URIs
  sip_add_log_entry("info", "Calculating digest response");

  // Calculate HA1 = MD5(username:realm:password)
  snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
  calculate_md5_hex(ha1_input, ha1);
  // Log HA1 safely
  sip_add_log_entry("info", "HA1 calculated");

  // Calculate HA2 = MD5(method:uri)
  snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, uri);
  calculate_md5_hex(ha2_input, ha2);
  // Log HA2 safely
  sip_add_log_entry("info", "HA2 calculated");

  // Calculate response
  if (qop && strlen(qop) > 0 && strcmp(qop, "auth") == 0) {
    // With qop=auth: MD5(HA1:nonce:nc:cnonce:qop:HA2)
    snprintf(response_input, sizeof(response_input), "%s:%s:%s:%s:%s:%s", ha1,
             nonce, nc, cnonce, qop, ha2);
    // Log response input safely
    sip_add_log_entry("info", "Response input (with qop): calculated");
  } else {
    // Without qop: MD5(HA1:nonce:HA2)
    snprintf(response_input, sizeof(response_input), "%s:%s:%s", ha1, nonce,
             ha2);
    // Log response input safely
    sip_add_log_entry("info", "Response input (no qop): calculated");
  }

  calculate_md5_hex(response_input, response_out);
  sip_add_log_entry("info", "Digest response calculated");
}
