#include "api_system.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_server.h"
#include "web_utils.h"
#include "wifi_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *TAG = "API_SYSTEM";

// ============================================================================
// System API Handlers
// ============================================================================

static esp_err_t get_system_state_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  uint32_t uptime_seconds = uptime_ms / 1000;
  uint32_t hours = uptime_seconds / 3600;
  uint32_t minutes = (uptime_seconds % 3600) / 60;
  uint32_t seconds = uptime_seconds % 60;

  char uptime_str[32];
  snprintf(uptime_str, sizeof(uptime_str), "%02u:%02u:%02u",
           (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds);
  cJSON_AddStringToObject(root, "uptime", uptime_str);

  uint32_t free_heap = esp_get_free_heap_size();
  char heap_str[32];
  snprintf(heap_str, sizeof(heap_str), "%u KB",
           (unsigned int)(free_heap / 1024));
  cJSON_AddStringToObject(root, "free_heap", heap_str);

  wifi_connection_info_t wifi_info = wifi_get_connection_info();
  cJSON_AddStringToObject(root, "ip_address", wifi_info.ip_address);
  cJSON_AddStringToObject(root, "firmware_version", "v1.1.0");
  cJSON_AddNumberToObject(root, "free_heap_bytes", free_heap);
  cJSON_AddNumberToObject(root, "uptime_ms", uptime_ms);

  return http_response_json_data(req, root);
}

static esp_err_t post_system_restart_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "System restart requested");

  cJSON *response = cJSON_CreateObject();
  if (response) {
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "System restart initiated");
    cJSON_AddBoolToObject(response, "session_invalidated", true);
    cJSON_AddStringToObject(response, "redirect_to", "/login.html");
    http_response_json_data(req, response);
  }

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}

static esp_err_t post_system_factory_reset_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Factory reset requested");

  cJSON *response = cJSON_CreateObject();
  if (response) {
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(
        response, "message",
        "Factory reset initiated. Device will restart with default settings.");
    cJSON_AddBoolToObject(response, "session_invalidated", true);
    cJSON_AddStringToObject(response, "redirect_to", "/login.html");
    http_response_json_data(req, response);
  }

  nvs_flash_erase();
  nvs_flash_init();

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}

static esp_err_t get_system_info_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK) {
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");

  uint32_t flash_size = 0;
  cJSON *partitions_array = cJSON_CreateArray();
  size_t total_used = 0;

  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *part = esp_partition_get(it);
    cJSON *partition = cJSON_CreateObject();
    if (partition) {
      cJSON_AddStringToObject(partition, "label", part->label);

      const char *type_str =
          (part->type == ESP_PARTITION_TYPE_APP) ? "app" : "data";
      cJSON_AddStringToObject(partition, "type", type_str);

      cJSON_AddNumberToObject(partition, "address", (double)part->address);
      cJSON_AddNumberToObject(partition, "size", (double)part->size);

      cJSON_AddItemToArray(partitions_array, partition);
    }
    total_used += part->size;
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  cJSON_AddItemToObject(root, "partitions", partitions_array);

  // Approximate flash size based on last partition end
  size_t max_address = 0;
  esp_partition_iterator_t it2 = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it2 != NULL) {
    const esp_partition_t *part = esp_partition_get(it2);
    size_t part_end = part->address + part->size;
    if (part_end > max_address)
      max_address = part_end;
    it2 = esp_partition_next(it2);
  }
  esp_partition_iterator_release(it2);

  if (max_address <= 2 * 1024 * 1024)
    flash_size = 2 * 1024 * 1024;
  else if (max_address <= 4 * 1024 * 1024)
    flash_size = 4 * 1024 * 1024;
  else if (max_address <= 8 * 1024 * 1024)
    flash_size = 8 * 1024 * 1024;
  else if (max_address <= 16 * 1024 * 1024)
    flash_size = 16 * 1024 * 1024;
  else
    flash_size = 32 * 1024 * 1024;

  cJSON_AddNumberToObject(root, "flash_size_mb", flash_size / (1024 * 1024));
  cJSON_AddNumberToObject(root, "flash_used_bytes", (double)total_used);
  cJSON_AddNumberToObject(
      root, "flash_available_bytes",
      (double)(flash_size > total_used ? flash_size - total_used : 0));

  uint8_t mac[6] = {0};
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  cJSON_AddStringToObject(root, "mac_address", mac_str);

  cJSON_AddNumberToObject(root, "free_heap_bytes",
                          (double)esp_get_free_heap_size());
  cJSON_AddNumberToObject(
      root, "uptime_seconds",
      (double)((xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000));
  cJSON_AddStringToObject(root, "firmware_version", "v1.1.0");

  return http_response_json_data(req, root);
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t system_state_uri = {.uri = "/api/system/state",
                                             .method = HTTP_GET,
                                             .handler =
                                                 get_system_state_handler,
                                             .user_ctx = NULL};

static const httpd_uri_t system_restart_uri = {.uri = "/api/system/restart",
                                               .method = HTTP_POST,
                                               .handler =
                                                   post_system_restart_handler,
                                               .user_ctx = NULL};

static const httpd_uri_t system_factory_reset_uri = {
    .uri = "/api/system/factory-reset",
    .method = HTTP_POST,
    .handler = post_system_factory_reset_handler,
    .user_ctx = NULL};

static const httpd_uri_t system_info_uri = {.uri = "/api/system/info",
                                            .method = HTTP_GET,
                                            .handler = get_system_info_handler,
                                            .user_ctx = NULL};

esp_err_t api_system_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering System API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret;
  ret = httpd_register_uri_handler(server, &system_state_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &system_restart_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &system_factory_reset_uri);
  if (ret != ESP_OK)
    return ret;

  ret = httpd_register_uri_handler(server, &system_info_uri);
  if (ret != ESP_OK)
    return ret;

  return ESP_OK;
}
