#include "dns_responder.h"
#include "esp_log.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNS_RESPONDER";
static TaskHandle_t dns_task_handle = NULL;
static bool dns_running = false;
static char captive_ip[16] = "192.168.4.1";  // Default AP IP

// DNS header structure
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

// DNS question structure
typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} dns_question_t;

/**
 * @brief Parse DNS name from packet
 */
static int parse_dns_name(const uint8_t *data, int offset, char *name, int max_len)
{
    int name_len = 0;
    int jump = 0;
    int pos = offset;

    while (data[pos] != 0) {
        if ((data[pos] & 0xC0) == 0xC0) {
            // Compressed name - jump to offset
            jump = ((data[pos] & 0x3F) << 8) | data[pos + 1];
            pos = jump;
            continue;
        }

        int label_len = data[pos++];
        if (name_len + label_len + 1 >= max_len) {
            return -1;  // Name too long
        }

        if (name_len > 0) {
            name[name_len++] = '.';
        }

        memcpy(&name[name_len], &data[pos], label_len);
        name_len += label_len;
        pos += label_len;
    }

    name[name_len] = '\0';
    return pos - offset + 1;  // Return bytes consumed
}

/**
 * @brief Build DNS response packet
 */
static int build_dns_response(uint8_t *buffer, int buffer_size, uint16_t query_id, const char *domain_name)
{
    dns_header_t *header = (dns_header_t *)buffer;

    // DNS header
    header->id = query_id;
    header->flags = htons(0x8180);  // Response, authoritative
    header->qdcount = htons(1);     // 1 question
    header->ancount = htons(1);     // 1 answer
    header->nscount = htons(0);     // No authority
    header->arcount = htons(0);     // No additional

    int offset = sizeof(dns_header_t);

    // Question section - copy the original question
    // We need to properly copy the question section from the original query
    // For simplicity, we'll assume the question starts right after the header

    // Domain name (compressed pointer to question)
    buffer[offset++] = 0xC0;  // Compression pointer
    buffer[offset++] = 0x0C;  // Points to start of domain name in question

    // Question type and class (A record, IN class)
    buffer[offset++] = 0x00;  // QTYPE high byte
    buffer[offset++] = 0x01;  // QTYPE low byte (A record)
    buffer[offset++] = 0x00;  // QCLASS high byte
    buffer[offset++] = 0x01;  // QCLASS low byte (IN)

    // Answer section
    // Domain name (compressed pointer to question)
    buffer[offset++] = 0xC0;  // Compression pointer
    buffer[offset++] = 0x0C;  // Points to start of domain name in question

    // Answer: TYPE=A, CLASS=IN, TTL=300, RDATA length=4
    buffer[offset++] = 0x00;  // TYPE high byte
    buffer[offset++] = 0x01;  // TYPE low byte (A record)
    buffer[offset++] = 0x00;  // CLASS high byte
    buffer[offset++] = 0x01;  // CLASS low byte (IN)
    buffer[offset++] = 0x00;  // TTL high byte
    buffer[offset++] = 0x00;  // TTL
    buffer[offset++] = 0x01;  // TTL
    buffer[offset++] = 0x2C;  // TTL low byte (300 seconds)
    buffer[offset++] = 0x00;  // RDATA length high byte
    buffer[offset++] = 0x04;  // RDATA length low byte (4 bytes for IPv4)

    // RDATA: IP address
    struct in_addr addr;
    if (inet_aton(captive_ip, &addr) == 0) {
        ESP_LOGE(TAG, "Invalid IP address: %s", captive_ip);
        return -1;
    }
    memcpy(&buffer[offset], &addr.s_addr, 4);
    offset += 4;

    return offset;
}

/**
 * @brief DNS responder task
 */
static void dns_responder_task(void *pvParameters)
{
    ESP_LOGI(TAG, "DNS responder task started");

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket: %d", errno);
        dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket: %d", errno);
        close(sockfd);
        dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS responder listening on UDP port 53");

    uint8_t buffer[512];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (dns_running) {
        int recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&client_addr, &client_len);

        if (recv_len < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Timeout - check if we should still be running
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGE(TAG, "DNS recvfrom error: %d", errno);
            break;
        }

        if (recv_len < sizeof(dns_header_t)) {
            ESP_LOGW(TAG, "DNS packet too small: %d bytes", recv_len);
            continue;
        }

        dns_header_t *header = (dns_header_t *)buffer;

        // Check if this is a query (QR bit should be 0)
        if (ntohs(header->flags) & 0x8000) {
            continue;  // This is a response, ignore
        }

        uint16_t query_id = header->id;

        // Parse domain name (simplified - just log it)
        char domain_name[256] = {0};
        int name_offset = sizeof(dns_header_t);
        int consumed = parse_dns_name(buffer, name_offset, domain_name, sizeof(domain_name));

        if (consumed < 0) {
            ESP_LOGW(TAG, "Failed to parse DNS domain name");
            continue;
        }

        ESP_LOGI(TAG, "DNS query for: %s from %s", domain_name,
                inet_ntoa(client_addr.sin_addr));

        // Build response - copy the original query first, then modify
        uint8_t response_buffer[512];
        memcpy(response_buffer, buffer, recv_len);  // Copy original query

        int response_len = build_dns_response(response_buffer, sizeof(response_buffer), query_id, domain_name);
        if (response_len < 0) {
            ESP_LOGW(TAG, "Failed to build DNS response");
            continue;
        }

        // Send response
        int sent = sendto(sockfd, response_buffer, response_len, 0,
                         (struct sockaddr *)&client_addr, client_len);
        if (sent < 0) {
            ESP_LOGE(TAG, "DNS sendto error: %d", errno);
        } else {
            ESP_LOGI(TAG, "DNS response sent: %s -> %s", domain_name, captive_ip);
        }
    }

    close(sockfd);
    dns_running = false;
    ESP_LOGI(TAG, "DNS responder task stopped");
    vTaskDelete(NULL);
}

bool dns_responder_start(void)
{
    ESP_LOGI(TAG, "Starting DNS responder for IP: %s", captive_ip);

    if (dns_running) {
        ESP_LOGW(TAG, "DNS responder already running");
        return true;
    }

    dns_running = true;

    BaseType_t result = xTaskCreate(
        dns_responder_task,
        "dns_responder",
        4096,  // Stack size
        NULL,
        5,     // Priority
        &dns_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS responder task");
        dns_running = false;
        return false;
    }

    ESP_LOGI(TAG, "DNS responder started successfully");
    return true;
}

void dns_responder_stop(void)
{
    if (dns_running) {
        ESP_LOGI(TAG, "Stopping DNS responder");
        dns_running = false;

        if (dns_task_handle) {
            vTaskDelay(pdMS_TO_TICKS(500));  // Give task time to stop gracefully
            vTaskDelete(dns_task_handle);
            dns_task_handle = NULL;
        }

        ESP_LOGI(TAG, "DNS responder stopped");
    }
}

void dns_responder_set_ip(const char* ip_address)
{
    if (ip_address && strlen(ip_address) < sizeof(captive_ip)) {
        strcpy(captive_ip, ip_address);
        ESP_LOGI(TAG, "DNS responder IP set to: %s", captive_ip);
    } else {
        ESP_LOGE(TAG, "Invalid IP address for DNS responder");
    }
}