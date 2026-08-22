#ifndef SIP_UTILS_H
#define SIP_UTILS_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>


// Calculate MD5 hash and convert to hex string
void calculate_md5_hex(const char *input, char *output);

// Generate random cnonce for digest authentication
void generate_cnonce(char *cnonce_out, size_t len);

// Helper to extract quoted value from header
bool extract_quoted_value(const char *header, const char *key, char *dest,
                          size_t dest_size);

// Helper to extract received IP from Via header
bool extract_received_ip(const char *via_header, char *dest, size_t dest_size);

// Helper function to resolve hostname to IP address using getaddrinfo
bool resolve_hostname(const char *hostname, struct sockaddr_in *addr,
                      uint16_t port);

// Helper function to get local IP address
bool get_local_ip(char *ip_str, size_t max_len);

// MD5 implementation if not available via mbedtls (or just wrapper)
// Note: ESP-IDF has mbedtls
#include "mbedtls/md5.h"

#endif // SIP_UTILS_H
