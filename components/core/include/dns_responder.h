#ifndef DNS_RESPONDER_H
#define DNS_RESPONDER_H

#include <stdbool.h>

/**
 * @brief Start the DNS responder on UDP port 53
 *
 * This starts a simple DNS server that responds to all A queries
 * with a configured IP address (default: 192.168.4.1). This enables
 * captive portal functionality by hijacking DNS queries.
 *
 * @return true if started successfully, false otherwise
 */
bool dns_responder_start(void);

/**
 * @brief Stop the DNS responder
 *
 * This stops the DNS responder and frees associated resources.
 */
void dns_responder_stop(void);

/**
 * @brief Set the IP address to return for DNS queries
 *
 * @param ip_address IP address as string (e.g., "192.168.4.1")
 */
void dns_responder_set_ip(const char* ip_address);

#endif // DNS_RESPONDER_H