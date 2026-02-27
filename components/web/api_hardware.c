#include "api_hardware.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "hardware_test.h"
#include "web_server.h"
#include "web_utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "API_HARDWARE";

// ============================================================================
// Hardware API Handlers
// ============================================================================

static esp_err_t post_hardware_test_doorbell_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *bell = cJSON_GetObjectItem(root, "bell");

  if (!cJSON_IsNumber(bell)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing bell number");
  }

  int bell_num = bell->valueint;
  doorbell_t doorbell = (bell_num == 1) ? DOORBELL_1 : DOORBELL_2;
  esp_err_t result = hardware_test_doorbell(doorbell);
  cJSON_Delete(root);

  if (result == ESP_OK) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Doorbell %d test triggered", bell_num);
    return http_response_json_success(req, msg);
  } else {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid bell number (must be 1 or 2)");
  }
}

static esp_err_t post_hardware_test_door_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *duration = cJSON_GetObjectItem(root, "duration");

  if (!cJSON_IsNumber(duration)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing duration");
  }

  uint32_t duration_ms = (uint32_t)duration->valueint;
  esp_err_t result = hardware_test_door_opener(duration_ms);
  cJSON_Delete(root);

  if (result == ESP_OK) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Door opener activated for %u ms",
             (unsigned int)duration_ms);
    return http_response_json_success(req, msg);
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Door opener test failed");
  }
}

static esp_err_t post_hardware_test_light_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  bool new_state = false;
  esp_err_t result = hardware_test_light_toggle(&new_state);

  if (result == ESP_OK) {
    cJSON *response = cJSON_CreateObject();
    if (response) {
      cJSON_AddBoolToObject(response, "success", true);
      cJSON_AddStringToObject(response, "state", new_state ? "on" : "off");
      return http_response_json_data(req, response);
    }
    return ESP_FAIL;
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Light toggle failed");
  }
}

static esp_err_t get_hardware_state_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  hardware_state_t state;
  hardware_test_get_state(&state);

  cJSON *response = cJSON_CreateObject();
  if (response) {
    cJSON_AddBoolToObject(response, "door_relay_active",
                          state.door_relay_active);
    cJSON_AddBoolToObject(response, "light_relay_active",
                          state.light_relay_active);
    cJSON_AddBoolToObject(response, "bell1_pressed", state.bell1_pressed);
    cJSON_AddBoolToObject(response, "bell2_pressed", state.bell2_pressed);
    cJSON_AddNumberToObject(response, "door_relay_remaining_ms",
                            (double)state.door_relay_remaining_ms);
    return http_response_json_data(req, response);
  }

  return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                  "Failed to create JSON response");
}

static esp_err_t post_hardware_test_stop_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  esp_err_t result = hardware_test_stop_all();

  if (result == ESP_OK) {
    return http_response_json_success(req, "All tests stopped");
  } else {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Emergency stop failed");
  }
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t hardware_test_doorbell_uri = {
    .uri = "/api/hardware/test/doorbell",
    .method = HTTP_POST,
    .handler = post_hardware_test_doorbell_handler,
    .user_ctx = NULL};

static const httpd_uri_t hardware_test_door_uri = {
    .uri = "/api/hardware/test/door",
    .method = HTTP_POST,
    .handler = post_hardware_test_door_handler,
    .user_ctx = NULL};

static const httpd_uri_t hardware_test_light_uri = {
    .uri = "/api/hardware/test/light",
    .method = HTTP_POST,
    .handler = post_hardware_test_light_handler,
    .user_ctx = NULL};

static const httpd_uri_t hardware_state_uri = {.uri = "/api/hardware/state",
                                               .method = HTTP_GET,
                                               .handler =
                                                   get_hardware_state_handler,
                                               .user_ctx = NULL};

static const httpd_uri_t hardware_test_stop_uri = {
    .uri = "/api/hardware/test/stop",
    .method = HTTP_POST,
    .handler = post_hardware_test_stop_handler,
    .user_ctx = NULL};

esp_err_t api_hardware_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Hardware API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &hardware_test_doorbell_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &hardware_test_door_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &hardware_test_light_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &hardware_state_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &hardware_test_stop_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
