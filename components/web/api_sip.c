#include "api_sip.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sip_client.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "API_SIP";

// ============================================================================
// SIP API Handlers
// ============================================================================

static esp_err_t get_sip_state_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  sip_state_t state = sip_client_get_state();
  bool registered = sip_is_registered();

  cJSON_AddStringToObject(root, "status", sip_state_to_str(state));
  cJSON_AddBoolToObject(root, "registered", registered);

  char status_buf[128];
  sip_get_status(status_buf, sizeof(status_buf));
  cJSON_AddStringToObject(root, "status_text", status_buf);

  return http_response_json_data(req, root);
}

static esp_err_t get_sip_config_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  sip_config_t config;
  sip_get_config(&config);

  cJSON_AddStringToObject(root, "server", config.server);
  cJSON_AddNumberToObject(root, "port", config.port);
  cJSON_AddStringToObject(root, "username", config.username);
  cJSON_AddStringToObject(root, "apartment1_uri", config.apartment1_uri);
  cJSON_AddStringToObject(root, "apartment2_uri", config.apartment2_uri);
  cJSON_AddBoolToObject(root, "configured", config.configured);

  return http_response_json_data(req, root);
}

static esp_err_t post_sip_config_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  sip_config_t config;
  sip_get_config(&config);

  const cJSON *server = cJSON_GetObjectItem(root, "server");
  const cJSON *port = cJSON_GetObjectItem(root, "port");
  const cJSON *username = cJSON_GetObjectItem(root, "username");
  const cJSON *password = cJSON_GetObjectItem(root, "password");
  const cJSON *apt1 = cJSON_GetObjectItem(root, "apartment1_uri");
  const cJSON *apt2 = cJSON_GetObjectItem(root, "apartment2_uri");

  if (cJSON_IsString(server) && server->valuestring != NULL) {
    strncpy(config.server, server->valuestring, sizeof(config.server) - 1);
  }
  if (cJSON_IsNumber(port)) {
    config.port = port->valueint;
  }
  if (cJSON_IsString(username) && username->valuestring != NULL) {
    strncpy(config.username, username->valuestring,
            sizeof(config.username) - 1);
  }
  if (cJSON_IsString(password) && password->valuestring != NULL) {
    strncpy(config.password, password->valuestring,
            sizeof(config.password) - 1);
  }
  if (cJSON_IsString(apt1) && apt1->valuestring != NULL) {
    strncpy(config.apartment1_uri, apt1->valuestring,
            sizeof(config.apartment1_uri) - 1);
  }
  if (cJSON_IsString(apt2) && apt2->valuestring != NULL) {
    strncpy(config.apartment2_uri, apt2->valuestring,
            sizeof(config.apartment2_uri) - 1);
  }

  config.configured = true;

  cJSON_Delete(root);
  sip_set_config(&config);
  sip_save_config_from_struct(&config);

  return http_response_json_success(req, "SIP configuration updated");
}

static esp_err_t post_sip_test_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  bool success = sip_test_configuration();

  if (success) {
    return http_response_json_success(req, "SIP configuration test passed");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "SIP configuration test failed");
  }
}

static esp_err_t post_sip_test_call_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (!root) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *uri = cJSON_GetObjectItem(root, "uri");
  if (!cJSON_IsString(uri) || uri->valuestring == NULL) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing target URI");
  }

  sip_client_make_call(uri->valuestring);
  cJSON_Delete(root);

  return http_response_json_success(req, "Test call initiated");
}

static esp_err_t get_sip_log_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  sip_log_entry_t entries[20];
  int count = sip_get_log_entries(entries, 20, 0);

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  cJSON *array = cJSON_CreateArray();
  if (!array) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON array");
  }

  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_CreateObject();
    if (item) {
      cJSON_AddNumberToObject(item, "timestamp", (double)entries[i].timestamp);
      cJSON_AddStringToObject(item, "type", entries[i].type);
      cJSON_AddStringToObject(item, "message", entries[i].message);
      cJSON_AddItemToArray(array, item);
    }
  }

  cJSON_AddItemToObject(root, "logs", array);

  return http_response_json_data(req, root);
}

static esp_err_t post_sip_connect_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  bool success = sip_connect();
  if (success) {
    return http_response_json_success(req, "SIP connect initiated");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "SIP connect failed");
  }
}

static esp_err_t post_sip_disconnect_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  sip_disconnect();
  return http_response_json_success(req, "SIP disconnect initiated");
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t sip_state_uri = {.uri = "/api/sip/state",
                                          .method = HTTP_GET,
                                          .handler = get_sip_state_handler,
                                          .user_ctx = NULL};

static const httpd_uri_t sip_config_get_uri = {.uri = "/api/sip/config",
                                               .method = HTTP_GET,
                                               .handler =
                                                   get_sip_config_handler,
                                               .user_ctx = NULL};

static const httpd_uri_t sip_config_post_uri = {.uri = "/api/sip/config",
                                                .method = HTTP_POST,
                                                .handler =
                                                    post_sip_config_handler,
                                                .user_ctx = NULL};

static const httpd_uri_t sip_test_uri = {.uri = "/api/sip/test",
                                         .method = HTTP_POST,
                                         .handler = post_sip_test_handler,
                                         .user_ctx = NULL};

static const httpd_uri_t sip_test_call_uri = {.uri = "/api/sip/testcall",
                                              .method = HTTP_POST,
                                              .handler =
                                                  post_sip_test_call_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t sip_log_uri = {.uri = "/api/sip/log",
                                        .method = HTTP_GET,
                                        .handler = get_sip_log_handler,
                                        .user_ctx = NULL};

static const httpd_uri_t sip_connect_uri = {.uri = "/api/sip/connect",
                                            .method = HTTP_POST,
                                            .handler = post_sip_connect_handler,
                                            .user_ctx = NULL};

static const httpd_uri_t sip_disconnect_uri = {.uri = "/api/sip/disconnect",
                                               .method = HTTP_POST,
                                               .handler =
                                                   post_sip_disconnect_handler,
                                               .user_ctx = NULL};

esp_err_t api_sip_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering SIP API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &sip_state_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_config_get_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_config_post_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_test_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_test_call_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_log_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_connect_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &sip_disconnect_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
