#include "api_wifi.h"
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


static const char *TAG = "API_WIFI";

// ============================================================================
// WiFi API Handlers
// ============================================================================

static esp_err_t get_wifi_config_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  wifi_config_data_t config;
  wifi_manager_get_config(&config);

  cJSON_AddStringToObject(root, "ssid", config.ssid);
  cJSON_AddBoolToObject(root, "dhcp", config.dhcp);
  cJSON_AddStringToObject(root, "static_ip", config.static_ip);
  cJSON_AddStringToObject(root, "gateway", config.gateway);
  cJSON_AddStringToObject(root, "netmask", config.netmask);

  return http_response_json_data(req, root);
}

static esp_err_t post_wifi_config_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  wifi_config_data_t config;
  wifi_manager_get_config(&config);

  const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
  const cJSON *password = cJSON_GetObjectItem(root, "password");
  const cJSON *dhcp = cJSON_GetObjectItem(root, "dhcp");
  const cJSON *static_ip = cJSON_GetObjectItem(root, "static_ip");
  const cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
  const cJSON *netmask = cJSON_GetObjectItem(root, "netmask");

  if (cJSON_IsString(ssid) && ssid->valuestring != NULL) {
    strncpy(config.ssid, ssid->valuestring, sizeof(config.ssid) - 1);
  }
  if (cJSON_IsString(password) && password->valuestring != NULL) {
    strncpy(config.password, password->valuestring,
            sizeof(config.password) - 1);
  }
  if (cJSON_IsBool(dhcp)) {
    config.dhcp = cJSON_IsTrue(dhcp);
  }
  if (cJSON_IsString(static_ip) && static_ip->valuestring != NULL) {
    strncpy(config.static_ip, static_ip->valuestring,
            sizeof(config.static_ip) - 1);
  }
  if (cJSON_IsString(gateway) && gateway->valuestring != NULL) {
    strncpy(config.gateway, gateway->valuestring, sizeof(config.gateway) - 1);
  }
  if (cJSON_IsString(netmask) && netmask->valuestring != NULL) {
    strncpy(config.netmask, netmask->valuestring, sizeof(config.netmask) - 1);
  }

  cJSON_Delete(root);
  wifi_manager_set_config(&config);
  wifi_manager_save_config();

  return http_response_json_success(req, "WiFi configuration updated");
}

static esp_err_t get_wifi_state_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  wifi_connection_info_t info = wifi_get_connection_info();

  cJSON_AddStringToObject(root, "status",
                          info.connected ? "connected" : "disconnected");
  cJSON_AddStringToObject(root, "ssid", info.ssid);
  cJSON_AddNumberToObject(root, "rssi", info.rssi);
  cJSON_AddStringToObject(root, "ip_address", info.ip_address);
  cJSON_AddStringToObject(root, "gateway", info.gateway);
  cJSON_AddStringToObject(root, "netmask", info.netmask);

  return http_response_json_data(req, root);
}

static esp_err_t post_wifi_scan_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  wifi_scan_result_t *ap_records = NULL;
  int ap_count = wifi_scan_networks(&ap_records);

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    if (ap_records)
      free(ap_records);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");
  }

  cJSON *ap_list = cJSON_CreateArray();
  if (!ap_list) {
    cJSON_Delete(root);
    if (ap_records)
      free(ap_records);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON array");
  }

  if (ap_records && ap_count > 0) {
    for (int i = 0; i < ap_count; i++) {
      cJSON *ap = cJSON_CreateObject();
      if (ap) {
        cJSON_AddStringToObject(ap, "ssid", ap_records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_records[i].rssi);
        cJSON_AddBoolToObject(ap, "secure", ap_records[i].secure);
        cJSON_AddItemToArray(ap_list, ap);
      }
    }
    free(ap_records);
  }

  cJSON_AddItemToObject(root, "access_points", ap_list);
  cJSON_AddNumberToObject(root, "count", ap_count);

  return http_response_json_data(req, root);
}

static esp_err_t post_wifi_connect_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  wifi_manager_connect();
  return http_response_json_success(req, "WiFi connect initiated");
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t wifi_config_get_uri = {.uri = "/api/wifi/config",
                                                .method = HTTP_GET,
                                                .handler =
                                                    get_wifi_config_handler,
                                                .user_ctx = NULL};

static const httpd_uri_t wifi_config_post_uri = {.uri = "/api/wifi/config",
                                                 .method = HTTP_POST,
                                                 .handler =
                                                     post_wifi_config_handler,
                                                 .user_ctx = NULL};

static const httpd_uri_t wifi_state_uri = {.uri = "/api/wifi/state",
                                           .method = HTTP_GET,
                                           .handler = get_wifi_state_handler,
                                           .user_ctx = NULL};

static const httpd_uri_t wifi_scan_uri = {.uri = "/api/wifi/scan",
                                          .method = HTTP_POST,
                                          .handler = post_wifi_scan_handler,
                                          .user_ctx = NULL};

static const httpd_uri_t wifi_connect_uri = {.uri = "/api/wifi/connect",
                                             .method = HTTP_POST,
                                             .handler =
                                                 post_wifi_connect_handler,
                                             .user_ctx = NULL};

esp_err_t api_wifi_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering WiFi API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &wifi_config_get_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &wifi_config_post_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &wifi_state_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &wifi_scan_uri);
  if (ret != ESP_OK)
    return ret;
  ret = httpd_register_uri_handler(server, &wifi_connect_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
