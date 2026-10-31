#include "api_config.h"
#include "cJSON.h"
#include "dtmf_decoder.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static void config_restore_restart_task(void *pvParameters) {
  (void)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(1500));
  ESP_LOGI(TAG, "Restarting device after configuration restore");
  esp_restart();
  vTaskDelete(NULL);
}

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

  const esp_app_desc_t *app_desc = esp_app_get_description();
  cJSON_AddStringToObject(root, "firmware_version",
                          app_desc ? app_desc->version : "1.0.0");
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

static esp_err_t post_config_restore_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = http_parse_json_body(req);
  if (!root) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON body");
  }

  const cJSON *cfg = cJSON_GetObjectItem(root, "config");
  if (!cJSON_IsObject(cfg)) {
    cfg = root; // Allow direct config object or wrapped {"config": {...}}
  }

  const cJSON *wifi = cJSON_GetObjectItem(cfg, "wifi");
  if (cJSON_IsObject(wifi)) {
    wifi_config_data_t wifi_cfg;
    wifi_manager_get_config(&wifi_cfg);

    const cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(wifi, "password");
    const cJSON *dhcp = cJSON_GetObjectItem(wifi, "dhcp");
    const cJSON *static_ip = cJSON_GetObjectItem(wifi, "static_ip");
    const cJSON *gateway = cJSON_GetObjectItem(wifi, "gateway");
    const cJSON *netmask = cJSON_GetObjectItem(wifi, "netmask");

    if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
      strncpy(wifi_cfg.ssid, ssid->valuestring, sizeof(wifi_cfg.ssid) - 1);
      wifi_cfg.ssid[sizeof(wifi_cfg.ssid) - 1] = '\0';
    }
    if (cJSON_IsString(pass) && strcmp(pass->valuestring, "********") != 0 &&
        pass->valuestring[0] != '\0') {
      strncpy(wifi_cfg.password, pass->valuestring,
              sizeof(wifi_cfg.password) - 1);
      wifi_cfg.password[sizeof(wifi_cfg.password) - 1] = '\0';
    }
    if (cJSON_IsBool(dhcp)) {
      wifi_cfg.dhcp = cJSON_IsTrue(dhcp);
    }
    if (cJSON_IsString(static_ip)) {
      strncpy(wifi_cfg.static_ip, static_ip->valuestring,
              sizeof(wifi_cfg.static_ip) - 1);
      wifi_cfg.static_ip[sizeof(wifi_cfg.static_ip) - 1] = '\0';
    }
    if (cJSON_IsString(gateway)) {
      strncpy(wifi_cfg.gateway, gateway->valuestring,
              sizeof(wifi_cfg.gateway) - 1);
      wifi_cfg.gateway[sizeof(wifi_cfg.gateway) - 1] = '\0';
    }
    if (cJSON_IsString(netmask)) {
      strncpy(wifi_cfg.netmask, netmask->valuestring,
              sizeof(wifi_cfg.netmask) - 1);
      wifi_cfg.netmask[sizeof(wifi_cfg.netmask) - 1] = '\0';
    }
    if (wifi_cfg.ssid[0] != '\0') {
      wifi_save_config(wifi_cfg.ssid, wifi_cfg.password);
    }
    wifi_manager_set_config(&wifi_cfg);
  }

  const cJSON *sip = cJSON_GetObjectItem(cfg, "sip");
  if (cJSON_IsObject(sip)) {
    sip_config_t sip_cfg;
    sip_get_config(&sip_cfg);

    const cJSON *server = cJSON_GetObjectItem(sip, "server");
    const cJSON *port = cJSON_GetObjectItem(sip, "port");
    const cJSON *username = cJSON_GetObjectItem(sip, "username");
    const cJSON *password = cJSON_GetObjectItem(sip, "password");
    const cJSON *apt1 = cJSON_GetObjectItem(sip, "apartment1_uri");
    const cJSON *apt2 = cJSON_GetObjectItem(sip, "apartment2_uri");

    if (cJSON_IsString(server)) {
      strncpy(sip_cfg.server, server->valuestring, sizeof(sip_cfg.server) - 1);
      sip_cfg.server[sizeof(sip_cfg.server) - 1] = '\0';
    }
    if (cJSON_IsNumber(port) && port->valueint > 0 && port->valueint <= 65535) {
      sip_cfg.port = port->valueint;
    }
    if (cJSON_IsString(username)) {
      strncpy(sip_cfg.username, username->valuestring,
              sizeof(sip_cfg.username) - 1);
      sip_cfg.username[sizeof(sip_cfg.username) - 1] = '\0';
    }
    if (cJSON_IsString(password) &&
        strcmp(password->valuestring, "********") != 0 &&
        password->valuestring[0] != '\0') {
      strncpy(sip_cfg.password, password->valuestring,
              sizeof(sip_cfg.password) - 1);
      sip_cfg.password[sizeof(sip_cfg.password) - 1] = '\0';
    }
    if (cJSON_IsString(apt1)) {
      strncpy(sip_cfg.apartment1_uri, apt1->valuestring,
              sizeof(sip_cfg.apartment1_uri) - 1);
      sip_cfg.apartment1_uri[sizeof(sip_cfg.apartment1_uri) - 1] = '\0';
    }
    if (cJSON_IsString(apt2)) {
      strncpy(sip_cfg.apartment2_uri, apt2->valuestring,
              sizeof(sip_cfg.apartment2_uri) - 1);
      sip_cfg.apartment2_uri[sizeof(sip_cfg.apartment2_uri) - 1] = '\0';
    }
    sip_cfg.configured = (sip_cfg.server[0] != '\0');
    sip_save_config(sip_cfg.server, sip_cfg.username, sip_cfg.password,
                    sip_cfg.apartment1_uri, sip_cfg.apartment2_uri,
                    sip_cfg.port);
  }

  const cJSON *ntp = cJSON_GetObjectItem(cfg, "ntp");
  if (cJSON_IsObject(ntp)) {
    const cJSON *server = cJSON_GetObjectItem(ntp, "server");
    const cJSON *timezone = cJSON_GetObjectItem(ntp, "timezone");
    const char *srv = (cJSON_IsString(server) && server->valuestring[0] != '\0')
                          ? server->valuestring
                          : ntp_get_server();
    const char *tz =
        (cJSON_IsString(timezone) && timezone->valuestring[0] != '\0')
            ? timezone->valuestring
            : ntp_get_timezone();
    ntp_set_config(srv, tz);
  }

  const cJSON *dtmf = cJSON_GetObjectItem(cfg, "dtmf_security");
  if (cJSON_IsObject(dtmf)) {
    dtmf_security_config_t dtmf_cfg;
    dtmf_get_security_config(&dtmf_cfg);

    const cJSON *pin_enabled = cJSON_GetObjectItem(dtmf, "pin_enabled");
    const cJSON *pin_code = cJSON_GetObjectItem(dtmf, "pin_code");
    const cJSON *timeout_ms = cJSON_GetObjectItem(dtmf, "timeout_ms");
    const cJSON *max_attempts = cJSON_GetObjectItem(dtmf, "max_attempts");

    if (cJSON_IsBool(pin_enabled)) {
      dtmf_cfg.pin_enabled = cJSON_IsTrue(pin_enabled);
    }
    if (cJSON_IsString(pin_code) &&
        strcmp(pin_code->valuestring, "********") != 0 &&
        pin_code->valuestring[0] != '\0') {
      strncpy(dtmf_cfg.pin_code, pin_code->valuestring,
              sizeof(dtmf_cfg.pin_code) - 1);
      dtmf_cfg.pin_code[sizeof(dtmf_cfg.pin_code) - 1] = '\0';
    }
    if (cJSON_IsNumber(timeout_ms) && timeout_ms->valueint > 0) {
      dtmf_cfg.timeout_ms = (uint32_t)timeout_ms->valueint;
    }
    if (cJSON_IsNumber(max_attempts) && max_attempts->valueint > 0) {
      dtmf_cfg.max_attempts = (uint8_t)max_attempts->valueint;
    }
    dtmf_save_security_config(&dtmf_cfg);
  }

  cJSON_Delete(root);

  esp_err_t resp = http_response_json_success(
      req, "Configuration restored successfully. Restarting device...");
  xTaskCreate(config_restore_restart_task, "cfg_restart", 2048, NULL, 5, NULL);
  return resp;
}

static httpd_uri_t config_backup_uri = {
    .uri = "/api/config/backup",
    .method = HTTP_GET,
    .handler = get_config_backup_handler,
    .user_ctx = NULL};

static httpd_uri_t config_restore_uri = {
    .uri = "/api/config/restore",
    .method = HTTP_POST,
    .handler = post_config_restore_handler,
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

  ret = httpd_register_uri_handler(server, &config_restore_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
