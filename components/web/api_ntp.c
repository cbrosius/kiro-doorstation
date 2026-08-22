#include "api_ntp.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ntp_sync.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "API_NTP";

// ============================================================================
// NTP API Handlers
// ============================================================================

static esp_err_t get_ntp_state_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  bool is_synced = ntp_is_synced();
  cJSON_AddBoolToObject(root, "synced", is_synced);

  if (is_synced) {
    char time_str[64];
    ntp_get_time_string(time_str, sizeof(time_str));
    cJSON_AddStringToObject(root, "current_time", time_str);
    cJSON_AddNumberToObject(root, "timestamp_ms",
                            (double)ntp_get_timestamp_ms());
  }

  time_t last_sync = ntp_get_last_sync_time();
  if (last_sync > 0) {
    cJSON_AddNumberToObject(root, "last_sync_timestamp", (double)last_sync);
    struct tm timeinfo;
    localtime_r(&last_sync, &timeinfo);
    char last_sync_str[64];
    strftime(last_sync_str, sizeof(last_sync_str), "%Y-%m-%d %H:%M:%S",
             &timeinfo);
    cJSON_AddStringToObject(root, "last_sync_time", last_sync_str);
  } else {
    cJSON_AddNumberToObject(root, "last_sync_timestamp", 0);
    cJSON_AddStringToObject(root, "last_sync_time", "Never");
  }

  cJSON_AddStringToObject(root, "server", ntp_get_server());
  cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

  return http_response_json_data(req, root);
}

static esp_err_t get_ntp_config_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  cJSON_AddStringToObject(root, "server", ntp_get_server());
  cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());

  return http_response_json_data(req, root);
}

static esp_err_t post_ntp_config_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *ntp_server = cJSON_GetObjectItem(root, "server");
  const cJSON *timezone = cJSON_GetObjectItem(root, "timezone");

  if (!cJSON_IsString(ntp_server) || (ntp_server->valuestring == NULL)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing server");
  }

  const char *tz = (cJSON_IsString(timezone) && timezone->valuestring)
                       ? timezone->valuestring
                       : "UTC0";

  ntp_set_config(ntp_server->valuestring, tz);
  cJSON_Delete(root);

  return http_response_json_success(req, "NTP configuration updated");
}

static esp_err_t post_ntp_sync_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  ntp_force_sync();
  return http_response_json_success(req, "NTP sync initiated");
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t ntp_state_uri = {.uri = "/api/ntp/state",
                                          .method = HTTP_GET,
                                          .handler = get_ntp_state_handler,
                                          .user_ctx = NULL};

static const httpd_uri_t ntp_config_get_uri = {.uri = "/api/ntp/config",
                                               .method = HTTP_GET,
                                               .handler =
                                                   get_ntp_config_handler,
                                               .user_ctx = NULL};

static const httpd_uri_t ntp_config_post_uri = {.uri = "/api/ntp/config",
                                                .method = HTTP_POST,
                                                .handler =
                                                    post_ntp_config_handler,
                                                .user_ctx = NULL};

static const httpd_uri_t ntp_sync_uri = {.uri = "/api/ntp/sync",
                                         .method = HTTP_POST,
                                         .handler = post_ntp_sync_handler,
                                         .user_ctx = NULL};

esp_err_t api_ntp_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering NTP API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &ntp_state_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &ntp_config_get_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &ntp_config_post_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &ntp_sync_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
