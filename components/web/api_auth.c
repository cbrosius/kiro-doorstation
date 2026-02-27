#include "api_auth.h"
#include "auth_manager.h"
#include "cJSON.h"
#include "cert_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "web_server.h"
#include "web_utils.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const char *TAG = "API_AUTH";

// ============================================================================
// Authentication API Handlers
// ============================================================================

static esp_err_t post_auth_login_handler(httpd_req_t *req) {
  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *username = cJSON_GetObjectItem(root, "username");
  const cJSON *password = cJSON_GetObjectItem(root, "password");

  if (!cJSON_IsString(username) || !cJSON_IsString(password)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing username or password");
  }

  char client_ip[AUTH_IP_ADDRESS_MAX_LEN] = "unknown";
  int sockfd = httpd_req_to_sockfd(req);
  struct sockaddr_in6 addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
    if (addr.sin6_family == AF_INET) {
      inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, client_ip,
                sizeof(client_ip));
    } else {
      inet_ntop(AF_INET6, &addr.sin6_addr, client_ip, sizeof(client_ip));
    }
  }

  if (auth_is_ip_blocked(client_ip)) {
    cJSON_Delete(root);
    return http_response_json_error(
        req, HTTPD_400_BAD_REQUEST,
        "IP address temporarily blocked due to too many failed attempts");
  }

  auth_result_t result =
      auth_login(username->valuestring, password->valuestring, client_ip);
  cJSON_Delete(root);

  if (result.authenticated) {
    char cookie[256];
    snprintf(cookie, sizeof(cookie),
             "session_id=%s; Secure; SameSite=Strict; Max-Age=%d; Path=/",
             result.session_id, AUTH_SESSION_TIMEOUT_SECONDS);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return http_response_json_success(req, "Login successful");
  } else {
    auth_record_failed_attempt(client_ip);
    return http_response_json_error(req, HTTPD_401_UNAUTHORIZED,
                                    "Invalid username or password");
  }
}

static esp_err_t post_auth_logout_handler(httpd_req_t *req) {
  char cookie[256];
  if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) ==
      ESP_OK) {
    char *sid = strstr(cookie, "session_id=");
    if (sid) {
      sid += 11;
      char *end = strchr(sid, ';');
      if (end)
        *end = '\0';
      auth_logout(sid);
    }
  }

  httpd_resp_set_hdr(req, "Set-Cookie", "session_id=; Max-Age=0; Path=/");
  return http_response_json_success(req, "Logout successful");
}

static esp_err_t post_auth_set_password_handler(httpd_req_t *req) {
  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *password = cJSON_GetObjectItem(root, "password");
  if (!cJSON_IsString(password)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing password");
  }

  esp_err_t err = auth_set_initial_password(password->valuestring);
  cJSON_Delete(root);

  if (err == ESP_OK) {
    if (!cert_exists())
      cert_generate_self_signed("doorstation.local", 3650);
    return http_response_json_success(
        req, "Initial password set and certificate generated");
  } else {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Failed to set initial password");
  }
}

static esp_err_t post_auth_change_password_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK)
    return ESP_FAIL;

  cJSON *root = http_parse_json_body(req);
  if (root == NULL) {
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Invalid or missing JSON");
  }

  const cJSON *curr = cJSON_GetObjectItem(root, "current_password");
  const cJSON *next = cJSON_GetObjectItem(root, "new_password");

  if (!cJSON_IsString(curr) || !cJSON_IsString(next)) {
    cJSON_Delete(root);
    return http_response_json_error(req, HTTPD_400_BAD_REQUEST,
                                    "Missing current or new password");
  }

  esp_err_t err = auth_change_password(curr->valuestring, next->valuestring);
  cJSON_Delete(root);

  if (err == ESP_OK) {
    return http_response_json_success(req, "Password changed successfully");
  } else {
    return http_response_json_error(
        req, HTTPD_400_BAD_REQUEST,
        "Failed to change password (invalid current password?)");
  }
}

static esp_err_t get_auth_logs_handler(httpd_req_t *req) {
  if (auth_filter(req, false) != ESP_OK)
    return ESP_FAIL;

  audit_log_entry_t *logs = malloc(50 * sizeof(audit_log_entry_t));
  if (!logs)
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Memory allocation failed");

  int count = auth_get_audit_logs(logs, 50);
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    free(logs);
    return http_response_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "Failed to create JSON");
  }

  cJSON *arr = cJSON_CreateArray();
  if (arr) {
    for (int i = 0; i < count; i++) {
      cJSON *entry = cJSON_CreateObject();
      if (entry) {
        cJSON_AddNumberToObject(entry, "timestamp", (double)logs[i].timestamp);
        cJSON_AddStringToObject(entry, "username", logs[i].username);
        cJSON_AddStringToObject(entry, "ip_address", logs[i].ip_address);
        cJSON_AddStringToObject(entry, "result", logs[i].result);
        cJSON_AddBoolToObject(entry, "success", logs[i].success);
        cJSON_AddItemToArray(arr, entry);
      }
    }
    cJSON_AddItemToObject(root, "logs", arr);
  }

  free(logs);
  return http_response_json_data(req, root);
}

// ============================================================================
// URI Handler Structures
// ============================================================================

static const httpd_uri_t auth_login_uri = {.uri = "/api/auth/login",
                                           .method = HTTP_POST,
                                           .handler = post_auth_login_handler,
                                           .user_ctx = NULL};

static const httpd_uri_t auth_logout_uri = {.uri = "/api/auth/logout",
                                            .method = HTTP_POST,
                                            .handler = post_auth_logout_handler,
                                            .user_ctx = NULL};

static const httpd_uri_t auth_setup_uri = {.uri = "/api/auth/setup",
                                           .method = HTTP_POST,
                                           .handler =
                                               post_auth_set_password_handler,
                                           .user_ctx = NULL};

static const httpd_uri_t auth_password_uri = {
    .uri = "/api/auth/password",
    .method = HTTP_POST,
    .handler = post_auth_change_password_handler,
    .user_ctx = NULL};

static const httpd_uri_t auth_logs_uri = {.uri = "/api/auth/logs",
                                          .method = HTTP_GET,
                                          .handler = get_auth_logs_handler,
                                          .user_ctx = NULL};

esp_err_t api_auth_register(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering Auth API handlers");
  if (server == NULL)
    return ESP_ERR_INVALID_ARG;

  httpd_register_uri_handler(server, &auth_login_uri);
  httpd_register_uri_handler(server, &auth_logout_uri);
  httpd_register_uri_handler(server, &auth_setup_uri);
  httpd_register_uri_handler(server, &auth_password_uri);
  httpd_register_uri_handler(server, &auth_logs_uri);

  return ESP_OK;
}
