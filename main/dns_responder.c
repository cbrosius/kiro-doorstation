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
#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

// DNS question structure
#pragma pack(push, 1)
typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} dns_question_t;
#pragma pack(pop)

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

        // Respond only to standard queries for A records
        if ((header->flags & 0x8000) == 0 && header->qdcount > 0) {
            // Find the end of the question section
            uint8_t *qname_end = buffer + sizeof(dns_header_t);
            while (*qname_end != 0 && (qname_end < buffer + recv_len)) {
                qname_end += *qname_end + 1;
            }
            qname_end++; // Skip the null terminator

            if (qname_end > buffer + recv_len - sizeof(dns_question_t)) {
                continue; // Malformed packet
            }

            dns_question_t *question = (dns_question_t *)qname_end;

            // We only care about A records (host address)
            if (question->qtype == htons(1) && question->qclass == htons(1)) {
                // Prepare response
                header->flags = htons(0x8180); // Standard response, no error
                header->ancount = htons(1);    // One answer

                int response_len = recv_len + 16; // Original length + answer length
                if (response_len > sizeof(buffer)) {
                    continue; // Response too long
                }

                uint8_t *answer_ptr = buffer + recv_len;

                // Answer section
                *answer_ptr++ = 0xC0; // Compressed name
                *answer_ptr++ = 0x0C; // Pointer to domain name in question

                *answer_ptr++ = 0x00; // Type A (high)
                *answer_ptr++ = 0x01; // Type A (low)

                *answer_ptr++ = 0x00; // Class IN (high)
                *answer_ptr++ = 0x01; // Class IN (low)

                // TTL (e.g., 60 seconds)
                *answer_ptr++ = 0x00;
                *answer_ptr++ = 0x00;
                *answer_ptr++ = 0x00;
                *answer_ptr++ = 0x3C;

                *answer_ptr++ = 0x00; // Data length 4 (high)
                *answer_ptr++ = 0x04; // Data length 4 (low)

                // IP Address
                struct in_addr addr;
                inet_aton(captive_ip, &addr);
                memcpy(answer_ptr, &addr.s_addr, 4);

                sendto(sockfd, buffer, response_len, 0, (struct sockaddr *)&client_addr, client_len);
            }
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
        4096,
        NULL,
        5,
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
            // No need to forcefully delete, the task will exit its loop
            // and delete itself. Just set the handle to NULL.
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
