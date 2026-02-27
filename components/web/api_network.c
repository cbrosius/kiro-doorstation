#include "api_network.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "web_server.h"
#include "web_utils.h"
#include "wifi_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "API_NETWORK";

// ============================================================================
// Network API Handlers
// ============================================================================

static esp_err_t get_network_ip_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  wifi_connection_info_t info = wifi_get_connection_info();

  cJSON_AddStringToObject(root, "ip_address", info.ip_address);
  cJSON_AddStringToObject(root, "subnet_mask", info.netmask);
  cJSON_AddStringToObject(root, "gateway", info.gateway);

  return http_response_json_data(req, root);
}

static esp_err_t post_network_ip_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  return http_response_json_success(
      req, "Static IP configuration is not yet implemented via this endpoint. "
           "Please use WiFi config.");
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t network_ip_get_uri = {.uri = "/api/network/ip",
                                               .method = HTTP_GET,
                                               .handler =
                                                   get_network_ip_handler,
                                               .user_ctx = NULL};

static const httpd_uri_t network_ip_post_uri = {.uri = "/api/network/ip",
                                                .method = HTTP_POST,
                                                .handler =
                                                    post_network_ip_handler,
                                                .user_ctx = NULL};

esp_err_t api_network_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Network API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &network_ip_get_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &network_ip_post_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
