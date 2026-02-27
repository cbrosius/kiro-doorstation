#include "web_utils.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "WEB_UTILS";

esp_err_t http_response_json_success(httpd_req_t *req, const char *message) {
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return ESP_FAIL;
  cJSON_AddStringToObject(root, "status", "success");
  if (message) {
    cJSON_AddStringToObject(root, "message", message);
  }
  return http_response_json_data(req, root);
}

esp_err_t http_response_json_error(httpd_req_t *req, httpd_err_code_t code,
                                   const char *message) {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, code, message);
    return ESP_FAIL;
  }
  cJSON_AddStringToObject(root, "status", "error");
  if (message) {
    cJSON_AddStringToObject(root, "message", message);
  }

  char *json_string = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");

  if (code == HTTPD_400_BAD_REQUEST)
    httpd_resp_set_status(req, "400 Bad Request");
  else if (code == HTTPD_401_UNAUTHORIZED)
    httpd_resp_set_status(req, "401 Unauthorized");
  else if (code == HTTPD_403_FORBIDDEN)
    httpd_resp_set_status(req, "403 Forbidden");
  else if (code == HTTPD_404_NOT_FOUND)
    httpd_resp_set_status(req, "404 Not Found");
  else if (code == HTTPD_500_INTERNAL_SERVER_ERROR)
    httpd_resp_set_status(req, "500 Internal Server Error");

  httpd_resp_send(req, json_string, -1);

  if (json_string)
    free(json_string);
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t http_response_json_data(httpd_req_t *req, cJSON *root) {
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON response");
  }

  char *json_string = cJSON_PrintUnformatted(root);
  if (json_string == NULL) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to print JSON");
  }

  httpd_resp_set_type(req, "application/json");
  esp_err_t ret = httpd_resp_send(req, json_string, -1);
  free(json_string);
  cJSON_Delete(root);
  return ret;
}

cJSON *http_parse_json_body(httpd_req_t *req) {
  if (req->content_len == 0) {
    ESP_LOGW(TAG, "Request body is empty");
    return NULL;
  }

  char *buf = malloc(req->content_len + 1);
  if (!buf) {
    ESP_LOGE(TAG, "Failed to allocate buffer for request body");
    return NULL;
  }

  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    return NULL;
  }
  buf[ret] = '\0';

  cJSON *root = cJSON_Parse(buf);
  free(buf);

  if (!root) {
    ESP_LOGE(TAG, "Failed to parse JSON body");
  }

  return root;
}
