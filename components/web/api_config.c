#include "api_config.h"
#include "cJSON.h"
#include "dtmf_decoder.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ntp_sync.h"
#include "sip_client.h"
#include "web_server.h"
#include "web_utils.h"
#include "wifi_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "API_CONFIG";

static esp_err_t get_config_backup_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  bool include_passwords = false;
  char query[64];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char param[16];
    if (httpd_query_key_value(query, "include_passwords", param, sizeof(param)) ==
        ESP_OK) {
      include_passwords = (strcmp(param, "true") == 0);
    }
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  cJSON_AddStringToObject(root, "firmware_version", "1.0.0");
  cJSON_AddBoolToObject(root, "include_passwords", include_passwords);

  cJSON *wifi = cJSON_CreateObject();
  wifi_config_data_t wifi_config;
  wifi_manager_get_config(&wifi_config);
  cJSON_AddStringToObject(wifi, "ssid", wifi_config.ssid);
  cJSON_AddStringToObject(wifi, "password",
                          include_passwords ? wifi_config.password : "********");
  cJSON_AddBoolToObject(wifi, "dhcp", wifi_config.dhcp);
  cJSON_AddStringToObject(wifi, "static_ip", wifi_config.static_ip);
  cJSON_AddStringToObject(wifi, "gateway", wifi_config.gateway);
  cJSON_AddStringToObject(wifi, "netmask", wifi_config.netmask);
  cJSON_AddItemToObject(root, "wifi", wifi);

  cJSON *sip = cJSON_CreateObject();
  sip_config_t sip_config;
  sip_get_config(&sip_config);
  cJSON_AddStringToObject(sip, "server", sip_config.server);
  cJSON_AddNumberToObject(sip, "port", sip_config.port);
  cJSON_AddStringToObject(sip, "username", sip_config.username);
  cJSON_AddStringToObject(sip, "password",
                          include_passwords ? sip_config.password : "********");
  cJSON_AddStringToObject(sip, "apartment1_uri", sip_config.apartment1_uri);
  cJSON_AddStringToObject(sip, "apartment2_uri", sip_config.apartment2_uri);
  cJSON_AddItemToObject(root, "sip", sip);

  cJSON *ntp = cJSON_CreateObject();
  cJSON_AddStringToObject(ntp, "server", ntp_get_server());
  cJSON_AddStringToObject(ntp, "timezone", ntp_get_timezone());
  cJSON_AddItemToObject(root, "ntp", ntp);

  cJSON *dtmf = cJSON_CreateObject();
  dtmf_security_config_t dtmf_config;
  dtmf_get_security_config(&dtmf_config);
  cJSON_AddBoolToObject(dtmf, "pin_enabled", dtmf_config.pin_enabled);
  cJSON_AddStringToObject(dtmf, "pin_code",
                          include_passwords ? dtmf_config.pin_code : "********");
  cJSON_AddNumberToObject(dtmf, "timeout_ms", dtmf_config.timeout_ms);
  cJSON_AddNumberToObject(dtmf, "max_attempts", dtmf_config.max_attempts);
  cJSON_AddItemToObject(root, "dtmf_security", dtmf);

  return http_response_json_data(req, root);
}

static httpd_uri_t config_backup_uri = {
    .uri = "/api/config/backup",
    .method = HTTP_GET,
    .handler = get_config_backup_handler,
    .user_ctx = NULL};

esp_err_t api_config_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Config API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &config_backup_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
