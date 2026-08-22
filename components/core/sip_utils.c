#include "sip_utils.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>


static const char *TAG = "SIP_UTILS";

// Calculate MD5 hash and convert to hex string
void calculate_md5_hex(const char *input, char *output) {
  if (!input || !output)
    return;
  unsigned char hash[16];

  // Use mbedtls_md5() which is available in ESP-IDF
  mbedtls_md5((const unsigned char *)input, strlen(input), hash);

  // Convert to hex string
  for (size_t i = 0; i < 16; i++) {
    sprintf(output + (i * 2), "%02x", hash[i]);
  }
  output[32] = '\0';
}

// Generate random cnonce for digest authentication
void generate_cnonce(char *cnonce_out, size_t len) {
  if (!cnonce_out || len < 17)
    return;
  uint32_t random1 = esp_random();
  uint32_t random2 = esp_random();
  snprintf(cnonce_out, len, "%08" PRIx32 "%08" PRIx32, random1, random2);
}

// Helper to extract quoted value from header
bool extract_quoted_value(const char *header, const char *key, char *dest,
                          size_t dest_size) {
  if (!header || !key || !dest)
    return false;
  const char *start = strstr(header, key);
  if (!start) {
    return false;
  }
  start += strlen(key);
  const char *end = strchr(start, '"');
  if (!end) {
    return false;
  }
  size_t len = end - start;
  if (len >= dest_size) {
    len = dest_size - 1;
  }
  strncpy(dest, start, len);
  dest[len] = '\0';
  return true;
}

// Helper to extract received IP from Via header
bool extract_received_ip(const char *via_header, char *dest, size_t dest_size) {
  if (!via_header || !dest)
    return false;
  const char *received_start = strstr(via_header, "received=");
  if (!received_start) {
    return false;
  }
  received_start += 9; // Skip "received="
  const char *received_end = strpbrk(received_start, ";\r\n ");
  if (!received_end) {
    return false;
  }
  size_t len = received_end - received_start;
  if (len >= dest_size) {
    len = dest_size - 1;
  }
  strncpy(dest, received_start, len);
  dest[len] = '\0';
  return true;
}

// Helper function to resolve hostname to IP address using getaddrinfo
bool resolve_hostname(const char *hostname, struct sockaddr_in *addr,
                      uint16_t port) {
  if (!hostname || !addr)
    return false;
  struct addrinfo hints;
  struct addrinfo *result = NULL;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;      // IPv4
  hints.ai_socktype = SOCK_DGRAM; // UDP
  hints.ai_protocol = IPPROTO_UDP;

  int ret = getaddrinfo(hostname, NULL, &hints, &result);
  if (ret != 0 || result == NULL) {
    ESP_LOGE(TAG, "DNS lookup failed for %s: %d", hostname, ret);
    if (result) {
      freeaddrinfo(result);
    }
    return false;
  }

  // Copy the resolved address
  memcpy(addr, result->ai_addr, sizeof(struct sockaddr_in));
  addr->sin_port = htons(port);

  freeaddrinfo(result);
  return true;
}

// Helper function to get local IP address
bool get_local_ip(char *ip_str, size_t max_len) {
  if (!ip_str)
    return false;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == NULL) {
    ESP_LOGW(TAG, "WiFi STA interface not found");
    return false;
  }

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
    snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    return true;
  }

  ESP_LOGW(TAG, "Failed to get IP address");
  return false;
}
