#include "api_cert.h"
#include "cJSON.h"
#include "cert_manager.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "API_CERT";
#define CERT_SAN_COUNT_MAX 16

// ============================================================================
// Certificate Management API Handlers
// ============================================================================

static esp_err_t get_cert_info_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  if (!cert_exists()) {
    return http_response_json_error(req, HTTPD_404_NOT_FOUND,
                                    "No certificate found");
  }

  cert_info_t info;
  esp_err_t err = cert_get_info(&info);

  if (err != ESP_OK) {
    return http_response_json_error(
        req, HTTPD_500_INTERNAL_SERVER_ERROR,
        "Failed to retrieve certificate information");
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  cJSON_AddBoolToObject(root, "exists", true);
  cJSON_AddBoolToObject(root, "is_self_signed", info.is_self_signed);
  cJSON_AddStringToObject(root, "common_name", info.common_name);
  cJSON_AddStringToObject(root, "issuer", info.issuer);
  cJSON_AddStringToObject(root, "not_before", info.not_before);
  cJSON_AddStringToObject(root, "not_after", info.not_after);
  cJSON_AddNumberToObject(root, "days_until_expiry",
                          (double)info.days_until_expiry);
  cJSON_AddBoolToObject(root, "is_expired", info.is_expired);
  cJSON_AddBoolToObject(root, "is_expiring_soon", info.is_expiring_soon);

  cJSON *san_array = cJSON_CreateArray();
  if (san_array) {
    for (int i = 0; i < info.san_count && i < CERT_SAN_COUNT_MAX; i++) {
      if (strlen(info.san_entries[i]) > 0) {
        cJSON_AddItemToArray(san_array,
                             cJSON_CreateString(info.san_entries[i]));
      }
    }
    cJSON_AddItemToObject(root, "san_entries", san_array);
  }
  cJSON_AddNumberToObject(root, "san_count", (double)info.san_count);

  return http_response_json_data(req, root);
}

static esp_err_t post_cert_upload_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON (max 16KB)");
  }

  const cJSON *cert_pem = cJSON_GetObjectItem(root, "cert_pem");
  const cJSON *key_pem = cJSON_GetObjectItem(root, "key_pem");
  const cJSON *chain_pem = cJSON_GetObjectItem(root, "chain_pem");

  if (!cert_pem)
    cert_pem = cJSON_GetObjectItem(root, "cert");
  if (!key_pem)
    key_pem = cJSON_GetObjectItem(root, "key");

  if (!cJSON_IsString(cert_pem) || !cJSON_IsString(key_pem)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing cert or key");
  }

  const char *chain_str = NULL;
  if (cJSON_IsString(chain_pem))
    chain_str = chain_pem->valuestring;

  esp_err_t err = cert_upload_custom(cert_pem->valuestring,
                                     key_pem->valuestring, chain_str);
  cJSON_Delete(root);

  if (err == ESP_OK) {
    cJSON *response = cJSON_CreateObject();
    if (response) {
      cJSON_AddBoolToObject(response, "success", true);
      cJSON_AddStringToObject(response, "message",
                              "Certificate uploaded. Restarting in 3 seconds.");
      cJSON_AddBoolToObject(response, "session_invalidated", true);
      cJSON_AddStringToObject(response, "redirect_to", "/login.html");
      http_response_json_data(req, response);
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
  } else {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    esp_err_to_name(err));
  }

  return ESP_OK;
}

static esp_err_t post_cert_generate_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  const char *cn = "doorstation.local";
  uint32_t validity = 3650;

  if (root) {
    const cJSON *common_name = cJSON_GetObjectItem(root, "common_name");
    const cJSON *validity_days = cJSON_GetObjectItem(root, "validity_days");
    if (cJSON_IsString(common_name))
      cn = common_name->valuestring;
    if (cJSON_IsNumber(validity_days))
      validity = validity_days->valueint;
  }

  esp_err_t err = cert_generate_self_signed(cn, validity);
  if (root)
    cJSON_Delete(root);

  if (err == ESP_OK) {
    return http_response_json_success(req,
                                      "Certificate generated successfully.");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to generate certificate");
  }
}

static esp_err_t get_cert_download_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK)
    return ESP_FAIL;

  if (!cert_exists()) {
    return http_response_json_error(req, HTTPD_404_NOT_FOUND,
                                    "No certificate found");
  }

  char *cert_pem = NULL;
  size_t cert_size = 0;
  if (cert_get_pem(&cert_pem, &cert_size) != ESP_OK) {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to get cert");
  }

  httpd_resp_set_type(req, "application/x-pem-file");
  httpd_resp_set_hdr(req, "Content-Disposition",
                     "attachment; filename=\"certificate.pem\"");
  httpd_resp_send(req, cert_pem, cert_size - 1);
  free(cert_pem);
  return ESP_OK;
}

static esp_err_t delete_cert_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK)
    return ESP_FAIL;

  if (!cert_exists()) {
    return http_response_json_error(req, HTTPD_404_NOT_FOUND,
                                    "No certificate found");
  }

  esp_err_t err = cert_delete();
  if (err == ESP_OK) {
    return http_response_json_success(req, "Certificate deleted");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to delete certificate");
  }
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t cert_info_uri = {.uri = "/api/cert/info",
                                          .method = HTTP_GET,
                                          .handler = get_cert_info_handler,
                                          .user_ctx = NULL};

static const httpd_uri_t cert_upload_uri = {.uri = "/api/cert/upload",
                                            .method = HTTP_POST,
                                            .handler = post_cert_upload_handler,
                                            .user_ctx = NULL};

static const httpd_uri_t cert_generate_uri = {.uri = "/api/cert/generate",
                                              .method = HTTP_POST,
                                              .handler =
                                                  post_cert_generate_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t cert_download_uri = {.uri = "/api/cert/download",
                                              .method = HTTP_GET,
                                              .handler =
                                                  get_cert_download_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t cert_delete_uri = {.uri = "/api/cert/delete",
                                            .method = HTTP_DELETE,
                                            .handler = delete_cert_handler,
                                            .user_ctx = NULL};

esp_err_t api_cert_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Certificate API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &cert_info_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &cert_upload_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &cert_generate_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &cert_download_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &cert_delete_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
