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

// Email configuration NVS functions
static void email_save_config(const email_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("email_config", NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, "smtp_server", config->smtp_server);
        nvs_set_u16(nvs_handle, "smtp_port", config->smtp_port);
        nvs_set_str(nvs_handle, "smtp_user", config->smtp_username);
        nvs_set_str(nvs_handle, "smtp_pass", config->smtp_password);
        nvs_set_str(nvs_handle, "recipient", config->recipient_email);
        nvs_set_u8(nvs_handle, "enabled", config->enabled ? 1 : 0);
        
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        
        ESP_LOGI(TAG, "Email configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for email config: %s", esp_err_to_name(err));
    }
}

static email_config_t email_load_config(void)
{
    email_config_t config = {0};
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("email_config", NVS_READONLY, &nvs_handle);
    
    if (err == ESP_OK) {
        size_t len;
        
        len = sizeof(config.smtp_server);
        if (nvs_get_str(nvs_handle, "smtp_server", config.smtp_server, &len) != ESP_OK) {
            config.smtp_server[0] = '\0';
        }
        
        if (nvs_get_u16(nvs_handle, "smtp_port", &config.smtp_port) != ESP_OK) {
            config.smtp_port = 587; // Default SMTP port
        }
        
        len = sizeof(config.smtp_username);
        if (nvs_get_str(nvs_handle, "smtp_user", config.smtp_username, &len) != ESP_OK) {
            config.smtp_username[0] = '\0';
        }
        
        len = sizeof(config.smtp_password);
        if (nvs_get_str(nvs_handle, "smtp_pass", config.smtp_password, &len) != ESP_OK) {
            config.smtp_password[0] = '\0';
        }
        
        len = sizeof(config.recipient_email);
        if (nvs_get_str(nvs_handle, "recipient", config.recipient_email, &len) != ESP_OK) {
            config.recipient_email[0] = '\0';
        }
        
        uint8_t enabled;
        if (nvs_get_u8(nvs_handle, "enabled", &enabled) == ESP_OK) {
            config.enabled = (enabled != 0);
        } else {
            config.enabled = false;
        }
        
        config.configured = (config.smtp_server[0] != '\0');
        
        nvs_close(nvs_handle);
    } else {
        config.configured = false;
        config.enabled = false;
        config.smtp_port = 587;
    }
    
    return config;
}

// Register all API endpoint handlers with the server
void web_api_register_handlers(httpd_handle_t server) {
    ESP_LOGI(TAG, "Registering API handlers");
    
    // API handler registration will be implemented in subsequent tasks
    (void)server; // Suppress unused parameter warning
    
    ESP_LOGI(TAG, "API handlers registered successfully");
}
