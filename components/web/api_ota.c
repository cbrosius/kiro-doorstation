#include "api_ota.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ota_handler.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "API_OTA";

// ============================================================================
// OTA API Handlers
// ============================================================================

static esp_err_t get_ota_info_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  ota_info_t info;
  ota_get_info(&info);

  cJSON_AddStringToObject(root, "version", info.version);
  cJSON_AddStringToObject(root, "build_date", info.build_date);
  cJSON_AddStringToObject(root, "idf_version", info.idf_version);
  cJSON_AddStringToObject(root, "partition", info.partition_label);
  cJSON_AddNumberToObject(root, "app_size", (double)info.app_size);
  cJSON_AddBoolToObject(root, "can_rollback", info.can_rollback);

  return http_response_json_data(req, root);
}

static esp_err_t post_ota_upload_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Starting OTA upload...");

  size_t total_len = req->content_len;
  if (total_len == 0) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Empty OTA file");
  }

  esp_err_t err = ota_begin_update(total_len);
  if (err != ESP_OK) {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to begin OTA update");
  }

  char *buf = malloc(4096);
  if (!buf) {
    ota_abort_update();
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Memory allocation failed");
  }

  size_t received = 0;
  while (received < total_len) {
    int chunk = httpd_req_recv(req, buf, 4096);
    if (chunk <= 0) {
      if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      free(buf);
      ota_abort_update();
      return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                      "OTA receive error");
    }

    err = ota_write_chunk((uint8_t *)buf, chunk);
    if (err != ESP_OK) {
      free(buf);
      ota_abort_update();
      return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                      "OTA write error");
    }

    received += chunk;
  }

  free(buf);

  err = ota_end_update();
  if (err != ESP_OK) {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "OTA finalization error");
  }

  return http_response_json_success(req, "OTA update complete, rebooting...");
}

static esp_err_t post_ota_rollback_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  esp_err_t err = ota_rollback();
  if (err == ESP_OK) {
    return http_response_json_success(req, "Rollback successful, rebooting...");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Rollback failed");
  }
}

static esp_err_t get_ota_status_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  const ota_context_t *ctx = ota_get_context();

  cJSON_AddStringToObject(root, "state", ota_get_status_string());
  cJSON_AddNumberToObject(root, "progress", ctx->progress_percent);
  cJSON_AddNumberToObject(root, "written", (double)ctx->written_size);
  cJSON_AddNumberToObject(root, "total", (double)ctx->total_size);
  if (ctx->error_message[0] != '\0') {
    cJSON_AddStringToObject(root, "error", ctx->error_message);
  }

  return http_response_json_data(req, root);
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t ota_info_uri = {.uri = "/api/ota/info",
                                         .method = HTTP_GET,
                                         .handler = get_ota_info_handler,
                                         .user_ctx = NULL};

static const httpd_uri_t ota_upload_uri = {.uri = "/api/ota/upload",
                                           .method = HTTP_POST,
                                           .handler = post_ota_upload_handler,
                                           .user_ctx = NULL};

static const httpd_uri_t ota_rollback_uri = {.uri = "/api/ota/rollback",
                                             .method = HTTP_POST,
                                             .handler =
                                                 post_ota_rollback_handler,
                                             .user_ctx = NULL};

static const httpd_uri_t ota_status_uri = {.uri = "/api/ota/status",
                                           .method = HTTP_GET,
                                           .handler = get_ota_status_handler,
                                           .user_ctx = NULL};

esp_err_t api_ota_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering OTA API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &ota_info_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &ota_upload_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &ota_rollback_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &ota_status_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
