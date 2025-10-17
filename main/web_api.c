#include "web_api.h"
#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sip_client.h"
#include "wifi_manager.h"
#include "ntp_sync.h"
#include "auth_manager.h"
#include "gpio_handler.h"
#include "hardware_test.h"
#include "cert_manager.h"
#include "dtmf_decoder.h"
#include <string.h>
#include <sys/socket.h>

static const char *TAG = "web_api";

// Email configuration structure
typedef struct {
    char smtp_server[64];
    uint16_t smtp_port;
    char smtp_username[64];
    char smtp_password[64];
    char recipient_email[64];
    bool enabled;
    bool configured;
} email_config_t;

// Register all API endpoint handlers with the server
void web_api_register_handlers(httpd_handle_t server) {
    ESP_LOGI(TAG, "Registering API handlers");
    
    // API handler registration will be implemented in subsequent tasks
    
    ESP_LOGI(TAG, "API handlers registered successfully");
}
