#include "api_dtmf.h"
#include "cJSON.h"
#include "dtmf_decoder.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "API_DTMF";

// ============================================================================
// DTMF API Handlers
// ============================================================================

static esp_err_t get_dtmf_security_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  dtmf_security_config_t config;
  dtmf_get_security_config(&config);

  cJSON_AddBoolToObject(root, "pin_enabled", config.pin_enabled);
  cJSON_AddStringToObject(root, "pin_code", config.pin_code);
  cJSON_AddNumberToObject(root, "timeout_ms", config.timeout_ms);
  cJSON_AddNumberToObject(root, "max_attempts", config.max_attempts);

  return http_response_json_data(req, root);
}

static esp_err_t post_dtmf_security_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  dtmf_security_config_t config;
  dtmf_get_security_config(&config);

  const cJSON *pin_enabled = cJSON_GetObjectItem(root, "pin_enabled");
  const cJSON *pin_code = cJSON_GetObjectItem(root, "pin_code");
  const cJSON *timeout_ms = cJSON_GetObjectItem(root, "timeout_ms");
  const cJSON *max_attempts = cJSON_GetObjectItem(root, "max_attempts");

  if (cJSON_IsBool(pin_enabled)) {
    config.pin_enabled = cJSON_IsTrue(pin_enabled);
  }

  if (cJSON_IsString(pin_code) && (pin_code->valuestring != NULL)) {
    strncpy(config.pin_code, pin_code->valuestring,
            sizeof(config.pin_code) - 1);
  }

  if (cJSON_IsNumber(timeout_ms)) {
    config.timeout_ms = (uint32_t)timeout_ms->valueint;
  }

  if (cJSON_IsNumber(max_attempts)) {
    config.max_attempts = (uint8_t)max_attempts->valueint;
  }

  cJSON_Delete(root);
  dtmf_save_security_config(&config);

  return http_response_json_success(req, "DTMF security configuration updated");
}

static esp_err_t get_dtmf_logs_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  char query[64];
  uint64_t since_timestamp = 0;

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(query, "since", param, sizeof(param)) == ESP_OK) {
      since_timestamp = strtoull(param, NULL, 10);
    }
  }

  const int max_entries = 50;
  dtmf_security_log_t *entries =
      malloc(max_entries * sizeof(dtmf_security_log_t));
  if (!entries) {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Out of memory");
  }

  int count = dtmf_get_security_logs(entries, max_entries, since_timestamp);

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    free(entries);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");
  }

  cJSON *logs_array = cJSON_CreateArray();
  if (!logs_array) {
    cJSON_Delete(root);
    free(entries);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON array");
  }

  for (int i = 0; i < count; i++) {
    cJSON *entry = cJSON_CreateObject();
    if (!entry)
      continue;

    cJSON_AddNumberToObject(entry, "timestamp", (double)entries[i].timestamp);

    const char *type_str;
    switch (entries[i].type) {
    case CMD_DOOR_OPEN:
      type_str = "door_open";
      break;
    case CMD_LIGHT_TOGGLE:
      type_str = "light_toggle";
      break;
    case CMD_CONFIG_CHANGE:
      type_str = "config_change";
      break;
    default:
      type_str = "invalid";
      break;
    }
    cJSON_AddStringToObject(entry, "type", type_str);
    cJSON_AddBoolToObject(entry, "success", entries[i].success);
    cJSON_AddStringToObject(entry, "command", entries[i].command);
    cJSON_AddStringToObject(entry, "caller", entries[i].caller_id);

    if (!entries[i].success && entries[i].reason[0] != '\0') {
      cJSON_AddStringToObject(entry, "reason", entries[i].reason);
    }

    cJSON_AddItemToArray(logs_array, entry);
  }

  cJSON_AddItemToObject(root, "logs", logs_array);
  cJSON_AddNumberToObject(root, "count", count);

  free(entries);
  return http_response_json_data(req, root);
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t dtmf_security_get_uri = {.uri = "/api/dtmf/security",
                                                  .method = HTTP_GET,
                                                  .handler =
                                                      get_dtmf_security_handler,
                                                  .user_ctx = NULL};

static const httpd_uri_t dtmf_security_post_uri = {
    .uri = "/api/dtmf/security",
    .method = HTTP_POST,
    .handler = post_dtmf_security_handler,
    .user_ctx = NULL};

static const httpd_uri_t dtmf_logs_uri = {.uri = "/api/dtmf/logs",
                                          .method = HTTP_GET,
                                          .handler = get_dtmf_logs_handler,
                                          .user_ctx = NULL};

esp_err_t api_dtmf_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering DTMF API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &dtmf_security_get_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &dtmf_security_post_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &dtmf_logs_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
