#include "api_email.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "API_EMAIL";

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
static void email_save_config(const email_config_t *config) {
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
    ESP_LOGE(TAG, "Failed to open NVS for email config: %s",
             esp_err_to_name(err));
  }
}

static email_config_t email_load_config(void) {
  email_config_t config = {0};
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("email_config", NVS_READONLY, &nvs_handle);

  if (err == ESP_OK) {
    size_t len;

    len = sizeof(config.smtp_server);
    if (nvs_get_str(nvs_handle, "smtp_server", config.smtp_server, &len) !=
        ESP_OK) {
      config.smtp_server[0] = '\0';
    }

    if (nvs_get_u16(nvs_handle, "smtp_port", &config.smtp_port) != ESP_OK) {
      config.smtp_port = 587; // Default SMTP port
    }

    len = sizeof(config.smtp_username);
    if (nvs_get_str(nvs_handle, "smtp_user", config.smtp_username, &len) !=
        ESP_OK) {
      config.smtp_username[0] = '\0';
    }

    len = sizeof(config.smtp_password);
    if (nvs_get_str(nvs_handle, "smtp_pass", config.smtp_password, &len) !=
        ESP_OK) {
      config.smtp_password[0] = '\0';
    }

    len = sizeof(config.recipient_email);
    if (nvs_get_str(nvs_handle, "recipient", config.recipient_email, &len) !=
        ESP_OK) {
      config.recipient_email[0] = '\0';
    }

    uint8_t enabled = 0;
    if (nvs_get_u8(nvs_handle, "enabled", &enabled) == ESP_OK) {
      config.enabled = (enabled != 0);
    } else {
      config.enabled = false;
    }

    config.configured = (config.smtp_server[0] != '\0');

    nvs_close(nvs_handle);
  } else {
    // Defaults if NVS fails
    config.smtp_port = 587;
    config.enabled = false;
    config.configured = false;
  }

  return config;
}

// ============================================================================
// Email API Handlers
// ============================================================================

static esp_err_t get_email_config_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  email_config_t config = email_load_config();

  cJSON_AddStringToObject(root, "smtp_server", config.smtp_server);
  cJSON_AddNumberToObject(root, "smtp_port", config.smtp_port);
  cJSON_AddStringToObject(root, "smtp_username", config.smtp_username);
  cJSON_AddStringToObject(root, "recipient_email", config.recipient_email);
  cJSON_AddBoolToObject(root, "enabled", config.enabled);
  cJSON_AddBoolToObject(root, "configured", config.configured);

  return http_response_json_data(req, root);
}

static esp_err_t post_email_config_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  email_config_t config = email_load_config();

  const cJSON *smtp_server = cJSON_GetObjectItem(root, "smtp_server");
  const cJSON *smtp_port = cJSON_GetObjectItem(root, "smtp_port");
  const cJSON *smtp_username = cJSON_GetObjectItem(root, "smtp_username");
  const cJSON *smtp_password = cJSON_GetObjectItem(root, "smtp_password");
  const cJSON *recipient_email = cJSON_GetObjectItem(root, "recipient_email");
  const cJSON *enabled = cJSON_GetObjectItem(root, "enabled");

  if (cJSON_IsString(smtp_server) && smtp_server->valuestring != NULL) {
    strncpy(config.smtp_server, smtp_server->valuestring,
            sizeof(config.smtp_server) - 1);
  }

  if (cJSON_IsNumber(smtp_port)) {
    config.smtp_port = (uint16_t)smtp_port->valueint;
  }

  if (cJSON_IsString(smtp_username) && smtp_username->valuestring != NULL) {
    strncpy(config.smtp_username, smtp_username->valuestring,
            sizeof(config.smtp_username) - 1);
  }

  if (cJSON_IsString(smtp_password) && smtp_password->valuestring != NULL) {
    strncpy(config.smtp_password, smtp_password->valuestring,
            sizeof(config.smtp_password) - 1);
  }

  if (cJSON_IsString(recipient_email) && recipient_email->valuestring != NULL) {
    strncpy(config.recipient_email, recipient_email->valuestring,
            sizeof(config.recipient_email) - 1);
  }

  if (cJSON_IsBool(enabled)) {
    config.enabled = cJSON_IsTrue(enabled);
  }

  config.configured = true;

  cJSON_Delete(root);
  email_save_config(&config);

  return http_response_json_success(req, "Email configuration saved");
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t email_config_get_uri = {.uri = "/api/email/config",
                                                 .method = HTTP_GET,
                                                 .handler =
                                                     get_email_config_handler,
                                                 .user_ctx = NULL};

static const httpd_uri_t email_config_post_uri = {.uri = "/api/email/config",
                                                  .method = HTTP_POST,
                                                  .handler =
                                                      post_email_config_handler,
                                                  .user_ctx = NULL};

esp_err_t api_email_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Email API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &email_config_get_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &email_config_post_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
