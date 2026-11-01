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
  wifi_config_data_t cfg;
  wifi_manager_get_config(&cfg);

  cJSON_AddStringToObject(root, "mode", cfg.dhcp ? "dhcp" : "static");
  cJSON_AddStringToObject(root, "ip",
                          (!cfg.dhcp && cfg.static_ip[0]) ? cfg.static_ip
                                                          : info.ip_address);
  cJSON_AddStringToObject(root, "subnet",
                          (!cfg.dhcp && cfg.netmask[0]) ? cfg.netmask
                                                        : info.netmask);
  cJSON_AddStringToObject(root, "gateway",
                          (!cfg.dhcp && cfg.gateway[0]) ? cfg.gateway
                                                        : info.gateway);
  cJSON_AddStringToObject(root, "dns1",
                          (!cfg.dhcp && cfg.dns1[0]) ? cfg.dns1 : info.dns1);
  cJSON_AddStringToObject(root, "dns2",
                          (!cfg.dhcp && cfg.dns2[0]) ? cfg.dns2 : info.dns2);

  return http_response_json_data(req, root);
}

static esp_err_t post_network_ip_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  wifi_config_data_t cfg;
  wifi_manager_get_config(&cfg);

  const cJSON *mode = cJSON_GetObjectItem(root, "mode");
  const cJSON *ip = cJSON_GetObjectItem(root, "ip");
  const cJSON *subnet = cJSON_GetObjectItem(root, "subnet");
  const cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
  const cJSON *dns1 = cJSON_GetObjectItem(root, "dns1");
  const cJSON *dns2 = cJSON_GetObjectItem(root, "dns2");

  if (cJSON_IsString(mode) && mode->valuestring != NULL) {
    cfg.dhcp = (strcmp(mode->valuestring, "static") != 0);
  }
  if (cJSON_IsString(ip) && ip->valuestring != NULL) {
    strncpy(cfg.static_ip, ip->valuestring, sizeof(cfg.static_ip) - 1);
    cfg.static_ip[sizeof(cfg.static_ip) - 1] = '\0';
  }
  if (cJSON_IsString(subnet) && subnet->valuestring != NULL) {
    strncpy(cfg.netmask, subnet->valuestring, sizeof(cfg.netmask) - 1);
    cfg.netmask[sizeof(cfg.netmask) - 1] = '\0';
  }
  if (cJSON_IsString(gateway) && gateway->valuestring != NULL) {
    strncpy(cfg.gateway, gateway->valuestring, sizeof(cfg.gateway) - 1);
    cfg.gateway[sizeof(cfg.gateway) - 1] = '\0';
  }
  if (cJSON_IsString(dns1) && dns1->valuestring != NULL) {
    strncpy(cfg.dns1, dns1->valuestring, sizeof(cfg.dns1) - 1);
    cfg.dns1[sizeof(cfg.dns1) - 1] = '\0';
  }
  if (cJSON_IsString(dns2) && dns2->valuestring != NULL) {
    strncpy(cfg.dns2, dns2->valuestring, sizeof(cfg.dns2) - 1);
    cfg.dns2[sizeof(cfg.dns2) - 1] = '\0';
  }

  cJSON_Delete(root);

  if (!cfg.dhcp &&
      (cfg.static_ip[0] == '\0' || cfg.netmask[0] == '\0' ||
       cfg.gateway[0] == '\0')) {
    return http_response_json_error(
        req, HTTPD_400_BAD_REQUEST,
        "Static IP mode requires IP address, subnet mask, and gateway");
  }

  wifi_manager_set_config(&cfg);
  wifi_manager_save_config();

  esp_err_t err = wifi_manager_apply_ip_config();
  if (err != ESP_OK) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Failed to apply IP configuration");
  }

  return http_response_json_success(req, "IP configuration updated");
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
