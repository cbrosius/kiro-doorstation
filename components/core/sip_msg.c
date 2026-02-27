#include "sip_msg.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "sip_client.h"
#include "sip_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdlib.h>

static const char *TAG = "SIP_MSG";

// Suppress format-truncation warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

// Extern declarations from sip_client.c (temporary until context is used)
extern int sip_socket;
extern sip_config_t sip_config;

// Extract common SIP headers from a request message
sip_request_headers_t extract_request_headers(const char *buffer) {
  sip_request_headers_t headers = {0};

  if (!buffer) {
    return headers;
  }

  // Extract Call-ID
  const char *cid_ptr = strstr(buffer, "Call-ID:");
  if (cid_ptr) {
    cid_ptr += 8; // Skip "Call-ID:"
    while (*cid_ptr == ' ')
      cid_ptr++; // Skip whitespace
    const char *cid_term = strstr(cid_ptr, "\r\n");
    if (cid_term && cid_ptr) {
      size_t len = cid_term - cid_ptr;
      if (len > 0 && len < sizeof(headers.call_id)) {
        strncpy(headers.call_id, cid_ptr, len);
        headers.call_id[len] = '\0';
      }
    }
  }

  // Extract Via (first Via header only)
  const char *via_ptr = strstr(buffer, "Via:");
  if (via_ptr) {
    via_ptr += 4; // Skip "Via:"
    while (*via_ptr == ' ')
      via_ptr++; // Skip whitespace
    const char *via_term = strstr(via_ptr, "\r\n");
    if (via_term && via_ptr) {
      size_t len = via_term - via_ptr;
      if (len > 0 && len < sizeof(headers.via_header)) {
        strncpy(headers.via_header, via_ptr, len);
        headers.via_header[len] = '\0';
      }
    }
  }

  // Extract From
  const char *from_ptr = strstr(buffer, "From:");
  if (from_ptr) {
    from_ptr += 5; // Skip "From:"
    while (*from_ptr == ' ')
      from_ptr++; // Skip whitespace
    const char *from_term = strstr(from_ptr, "\r\n");
    if (from_term && from_ptr) {
      size_t len = from_term - from_ptr;
      if (len > 0 && len < sizeof(headers.from_header)) {
        strncpy(headers.from_header, from_ptr, len);
        headers.from_header[len] = '\0';
      }
    }
  }

  // Extract To
  const char *to_ptr = strstr(buffer, "To:");
  if (to_ptr) {
    to_ptr += 3; // Skip "To:"
    while (*to_ptr == ' ')
      to_ptr++; // Skip whitespace
    const char *to_term = strstr(to_ptr, "\r\n");
    if (to_term && to_ptr) {
      size_t len = to_term - to_ptr;
      if (len > 0 && len < sizeof(headers.to_header)) {
        strncpy(headers.to_header, to_ptr, len);
        headers.to_header[len] = '\0';
      }
    }
  }

  // Extract Contact (optional)
  const char *contact_ptr = strstr(buffer, "Contact:");
  if (contact_ptr) {
    contact_ptr += 8; // Skip "Contact:"
    while (*contact_ptr == ' ')
      contact_ptr++; // Skip whitespace
    const char *contact_term = strstr(contact_ptr, "\r\n");
    if (contact_term && contact_ptr) {
      size_t len = contact_term - contact_ptr;
      if (len > 0 && len < sizeof(headers.contact)) {
        strncpy(headers.contact, contact_ptr, len);
        headers.contact[len] = '\0';
      }
    }
  }

  // Extract CSeq number and method
  const char *cseq_ptr = strstr(buffer, "CSeq:");
  if (cseq_ptr) {
    cseq_ptr += 5; // Skip "CSeq:"
    while (*cseq_ptr == ' ')
      cseq_ptr++; // Skip whitespace
    headers.cseq_num = atoi(cseq_ptr);

    // Extract method from CSeq line
    const char *method_ptr = cseq_ptr;
    while (*method_ptr && *method_ptr != ' ')
      method_ptr++; // Skip number
    if (*method_ptr == ' ') {
      method_ptr++; // Skip space
      const char *method_end = strstr(method_ptr, "\r\n");
      if (method_end && method_ptr) {
        size_t len = method_end - method_ptr;
        if (len > 0 && len < sizeof(headers.cseq_method)) {
          strncpy(headers.cseq_method, method_ptr, len);
          headers.cseq_method[len] = '\0';
        }
      }
    }
  }

  // Mark as valid if we have at least the core headers
  headers.valid =
      (strlen(headers.call_id) > 0 && strlen(headers.via_header) > 0 &&
       strlen(headers.from_header) > 0 && strlen(headers.to_header) > 0 &&
       headers.cseq_num > 0);

  return headers;
}

// Generic SIP response builder and sender
void send_sip_response(int code, const char *reason,
                       const sip_request_headers_t *req_headers,
                       const char *extra_headers, const char *body) {
  if (!req_headers || !req_headers->valid || !reason) {
    ESP_LOGE(TAG, "Invalid parameters for send_sip_response");
    return;
  }

  if (sip_socket < 0) {
    ESP_LOGW(TAG, "Cannot send response: socket not available");
    return;
  }

  static char response[2048];
  int len = 0;

  len += snprintf(response + len, sizeof(response) - len, "SIP/2.0 %d %s\r\n",
                  code, reason);

  len += snprintf(response + len, sizeof(response) - len,
                  "Via: %s\r\n"
                  "From: %s\r\n"
                  "To: %s\r\n"
                  "Call-ID: %s\r\n"
                  "CSeq: %d %s\r\n",
                  req_headers->via_header, req_headers->from_header,
                  req_headers->to_header, req_headers->call_id,
                  req_headers->cseq_num, req_headers->cseq_method);

  if (extra_headers && strlen(extra_headers) > 0) {
    len +=
        snprintf(response + len, sizeof(response) - len, "%s", extra_headers);
  }

  len += snprintf(response + len, sizeof(response) - len,
                  "User-Agent: ESP32-Doorbell/1.2\r\n");

  if (body && strlen(body) > 0) {
    len += snprintf(response + len, sizeof(response) - len,
                    "Content-Length: %d\r\n\r\n%s", strlen(body), body);
  } else {
    len += snprintf(response + len, sizeof(response) - len,
                    "Content-Length: 0\r\n\r\n");
  }

  if (len >= sizeof(response)) {
    ESP_LOGE(TAG, "Response buffer overflow (%d bytes)", len);
    sip_add_log_entry("error", "Response too large - buffer overflow");
    return;
  }

  struct sockaddr_in server_addr;
  if (resolve_hostname(sip_config.server, &server_addr,
                       (uint16_t)sip_config.port)) {
    int sent = sendto(sip_socket, response, len, 0,
                      (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (sent > 0) {
      char log_msg[128];
      snprintf(log_msg, sizeof(log_msg), "%d %s sent (%d bytes)", code, reason,
               sent);
      sip_add_log_entry("sent", log_msg);
    } else {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "Failed to send %d %s: error %d", code,
               reason, sent);
      sip_add_log_entry("error", err_msg);
    }
  } else {
    ESP_LOGE(TAG, "DNS lookup failed when sending response");
    sip_add_log_entry("error", "DNS lookup failed for response");
  }
}

// Send ACK for error responses to INVITE
void send_ack_for_error_response(const char *response_buffer) {
  if (!response_buffer)
    return;

  sip_request_headers_t headers = extract_request_headers(response_buffer);
  if (!headers.valid)
    return;

  char to_tag[32] = {0};
  const char *to_hdr = strstr(response_buffer, "To:");
  if (to_hdr) {
    const char *tag_ptr = strstr(to_hdr, "tag=");
    if (tag_ptr) {
      tag_ptr += 4;
      const char *tag_term = strpbrk(tag_ptr, ";\r\n ");
      if (tag_term) {
        size_t tag_length = tag_term - tag_ptr;
        if (tag_length < sizeof(to_tag)) {
          strncpy(to_tag, tag_ptr, tag_length);
          to_tag[tag_length] = '\0';
        }
      }
    }
  }

  static char ack_msg[1024];
  char request_uri[128] = {0};
  const char *uri_start = strstr(headers.to_header, "<sip:");
  if (uri_start) {
    uri_start += 1;
    const char *uri_end = strchr(uri_start, '>');
    if (uri_end) {
      size_t uri_len = uri_end - uri_start;
      if (uri_len < sizeof(request_uri)) {
        strncpy(request_uri, uri_start, uri_len);
        request_uri[uri_len] = '\0';
      }
    }
  }

  if (strlen(request_uri) == 0) {
    snprintf(request_uri, sizeof(request_uri), "sip:%s@%s", sip_config.username,
             sip_config.server);
  }

  snprintf(ack_msg, sizeof(ack_msg),
           "ACK %s SIP/2.0\r\n"
           "Via: %s\r\n"
           "Max-Forwards: 70\r\n"
           "From: %s\r\n"
           "To: %s\r\n"
           "Call-ID: %s\r\n"
           "CSeq: %d ACK\r\n"
           "User-Agent: ESP32-Doorbell/1.2\r\n"
           "Content-Length: 0\r\n\r\n",
           request_uri, headers.via_header, headers.from_header,
           headers.to_header, headers.call_id, headers.cseq_num);

  struct sockaddr_in server_addr;
  if (resolve_hostname(sip_config.server, &server_addr,
                       (uint16_t)sip_config.port)) {
    int sent = sendto(sip_socket, ack_msg, strlen(ack_msg), 0,
                      (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (sent > 0) {
      char log_msg[256];
      snprintf(log_msg, sizeof(log_msg), "ACK sent for Call-ID=%s (CSeq=%d)",
               headers.call_id, headers.cseq_num);
      sip_add_log_entry("sent", log_msg);
    }
  }
}
