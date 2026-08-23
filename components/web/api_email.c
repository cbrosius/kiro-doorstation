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
  char report_schedule[16];
  char report_time[8];
  uint8_t report_day;
  uint8_t report_day_of_month;
  bool include_status;
  bool include_logs;
  bool include_backup;
  uint64_t last_report_timestamp;
  bool last_report_success;
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
    nvs_set_str(nvs_handle, "report_schedule", config->report_schedule);
    nvs_set_str(nvs_handle, "report_time", config->report_time);
    nvs_set_u8(nvs_handle, "report_day", config->report_day);
    nvs_set_u8(nvs_handle, "report_day_of_month", config->report_day_of_month);
    nvs_set_u8(nvs_handle, "include_status", config->include_status ? 1 : 0);
    nvs_set_u8(nvs_handle, "include_logs", config->include_logs ? 1 : 0);
    nvs_set_u8(nvs_handle, "include_backup", config->include_backup ? 1 : 0);
    nvs_set_u64(nvs_handle, "last_report_timestamp", config->last_report_timestamp);
    nvs_set_u8(nvs_handle, "last_report_success", config->last_report_success ? 1 : 0);

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

    len = sizeof(config.report_schedule);
    if (nvs_get_str(nvs_handle, "report_schedule", config.report_schedule, &len) != ESP_OK) {
      strncpy(config.report_schedule, "daily", sizeof(config.report_schedule) - 1);
    }

    len = sizeof(config.report_time);
    if (nvs_get_str(nvs_handle, "report_time", config.report_time, &len) != ESP_OK) {
      strncpy(config.report_time, "08:00", sizeof(config.report_time) - 1);
    }

    uint8_t report_day = 0;
    if (nvs_get_u8(nvs_handle, "report_day", &report_day) == ESP_OK) {
      config.report_day = report_day;
    } else {
      config.report_day = 0;
    }

    uint8_t report_day_of_month = 1;
    if (nvs_get_u8(nvs_handle, "report_day_of_month", &report_day_of_month) == ESP_OK) {
      config.report_day_of_month = report_day_of_month;
    } else {
      config.report_day_of_month = 1;
    }

    uint8_t include_status = 1;
    if (nvs_get_u8(nvs_handle, "include_status", &include_status) == ESP_OK) {
      config.include_status = (include_status != 0);
    } else {
      config.include_status = true;
    }

    uint8_t include_logs = 1;
    if (nvs_get_u8(nvs_handle, "include_logs", &include_logs) == ESP_OK) {
      config.include_logs = (include_logs != 0);
    } else {
      config.include_logs = true;
    }

    uint8_t include_backup = 1;
    if (nvs_get_u8(nvs_handle, "include_backup", &include_backup) == ESP_OK) {
      config.include_backup = (include_backup != 0);
    } else {
      config.include_backup = true;
    }

    uint64_t last_report_timestamp = 0;
    if (nvs_get_u64(nvs_handle, "last_report_timestamp", &last_report_timestamp) == ESP_OK) {
      config.last_report_timestamp = last_report_timestamp;
    } else {
      config.last_report_timestamp = 0;
    }

    uint8_t last_report_success = 0;
    if (nvs_get_u8(nvs_handle, "last_report_success", &last_report_success) == ESP_OK) {
      config.last_report_success = (last_report_success != 0);
    } else {
      config.last_report_success = false;
    }

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

  cJSON *smtp = cJSON_CreateObject();
  if (smtp) {
    cJSON_AddStringToObject(smtp, "server", config.smtp_server);
    cJSON_AddNumberToObject(smtp, "port", config.smtp_port);
    cJSON_AddStringToObject(smtp, "username", config.smtp_username);
    cJSON_AddStringToObject(smtp, "sender", config.smtp_username);
    cJSON_AddItemToObject(root, "smtp", smtp);
  }

  cJSON *reports = cJSON_CreateObject();
  if (reports) {
    cJSON_AddBoolToObject(reports, "enabled", config.enabled);
    cJSON_AddStringToObject(reports, "recipient", config.recipient_email);
    cJSON_AddStringToObject(reports, "schedule", config.report_schedule);
    cJSON_AddStringToObject(reports, "time", config.report_time);
    char day_buf[4];
    snprintf(day_buf, sizeof(day_buf), "%d", config.report_day);
    cJSON_AddStringToObject(reports, "day", day_buf);
    char dom_buf[4];
    snprintf(dom_buf, sizeof(dom_buf), "%d", config.report_day_of_month);
    cJSON_AddStringToObject(reports, "day_of_month", dom_buf);
    cJSON_AddBoolToObject(reports, "include_status", config.include_status);
    cJSON_AddBoolToObject(reports, "include_logs", config.include_logs);
    cJSON_AddBoolToObject(reports, "include_backup", config.include_backup);
    cJSON_AddItemToObject(root, "reports", reports);
  }

  cJSON *last_report = cJSON_CreateObject();
  if (last_report) {
    cJSON_AddNumberToObject(last_report, "timestamp", (double)config.last_report_timestamp);
    cJSON_AddBoolToObject(last_report, "success", config.last_report_success);
    cJSON_AddNullToObject(last_report, "error");
    cJSON_AddItemToObject(root, "last_report", last_report);
  }

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

  const cJSON *reports = cJSON_GetObjectItem(root, "reports");
  if (cJSON_IsObject(reports)) {
    const cJSON *report_schedule = cJSON_GetObjectItem(reports, "schedule");
    const cJSON *report_time = cJSON_GetObjectItem(reports, "time");
    const cJSON *report_day = cJSON_GetObjectItem(reports, "day");
    const cJSON *report_day_of_month = cJSON_GetObjectItem(reports, "day_of_month");
    const cJSON *include_status = cJSON_GetObjectItem(reports, "include_status");
    const cJSON *include_logs = cJSON_GetObjectItem(reports, "include_logs");
    const cJSON *include_backup = cJSON_GetObjectItem(reports, "include_backup");

    if (cJSON_IsString(report_schedule) && report_schedule->valuestring != NULL) {
      strncpy(config.report_schedule, report_schedule->valuestring,
              sizeof(config.report_schedule) - 1);
    }
    if (cJSON_IsString(report_time) && report_time->valuestring != NULL) {
      strncpy(config.report_time, report_time->valuestring,
              sizeof(config.report_time) - 1);
    }
    if (cJSON_IsNumber(report_day)) {
      config.report_day = (uint8_t)report_day->valueint;
    }
    if (cJSON_IsNumber(report_day_of_month)) {
      config.report_day_of_month = (uint8_t)report_day_of_month->valueint;
    }
    if (cJSON_IsBool(include_status)) {
      config.include_status = cJSON_IsTrue(include_status);
    }
    if (cJSON_IsBool(include_logs)) {
      config.include_logs = cJSON_IsTrue(include_logs);
    }
    if (cJSON_IsBool(include_backup)) {
      config.include_backup = cJSON_IsTrue(include_backup);
    }
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
