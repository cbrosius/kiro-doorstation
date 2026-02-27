#pragma GCC diagnostic ignored "-Waddress"
#include "web_server.h"
#include "auth_manager.h"
#include "cert_manager.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_api.h"
#include "web_utils.h"


static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;
static httpd_handle_t redirect_server = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t
    documentation_html_start[] asm("_binary_documentation_html_start");
extern const uint8_t
    documentation_html_end[] asm("_binary_documentation_html_end");
extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");
extern const uint8_t setup_html_start[] asm("_binary_setup_html_start");
extern const uint8_t setup_html_end[] asm("_binary_setup_html_end");

/**
 * @brief Check if a URI is a public endpoint that doesn't require
 * authentication
 */
static bool is_public_endpoint(const char *uri) {
  if (!uri) {
    return false;
  }

  const char *public_endpoints[] = {"/api/auth/login",
                                    "/api/auth/setup", // Match api_auth.c
                                    "/login.html",     "/setup.html",
                                    "/favicon.ico",    NULL};

  for (int i = 0; public_endpoints[i] != NULL; i++) {
    if (strcmp(uri, public_endpoints[i]) == 0) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Authentication filter for HTTP requests
 */
esp_err_t auth_filter(httpd_req_t *req, bool extend_session) {
  if (is_public_endpoint(req->uri)) {
    return ESP_OK;
  }

  if (!auth_is_password_set()) {
    ESP_LOGW(TAG, "No password set - redirecting to setup page");
    if (strncmp(req->uri, "/api/", 5) == 0) {
      return http_response_json_error(req, HTTPD_403_FORBIDDEN,
                                      "Initial setup required");
    } else {
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", "/setup.html");
      httpd_resp_send(req, NULL, 0);
    }
    return ESP_FAIL;
  }

  char session_id[AUTH_SESSION_ID_SIZE] = {0};
  size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");

  if (cookie_len > 0 && cookie_len < 512) {
    char *cookie_str = malloc(cookie_len + 1);
    if (cookie_str) {
      if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_str,
                                      cookie_len + 1) == ESP_OK) {
        char *session_start = strstr(cookie_str, "session_id=");
        if (session_start) {
          session_start += 11;
          char *session_end = strchr(session_start, ';');
          size_t session_len = session_end
                                   ? (size_t)(session_end - session_start)
                                   : strlen(session_start);

          if (session_len > 0 && session_len < AUTH_SESSION_ID_SIZE) {
            memcpy(session_id, session_start, session_len);
            session_id[session_len] = '\0';
          }
        }
      }
      free(cookie_str);
    }
  }

  if (session_id[0] == '\0') {
    ESP_LOGW(TAG, "No session cookie found for %s", req->uri);
    if (strncmp(req->uri, "/api/", 5) == 0) {
      return http_response_json_error(req, HTTPD_401_UNAUTHORIZED,
                                      "Authentication required");
    } else {
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", "/login.html");
      httpd_resp_send(req, NULL, 0);
    }
    return ESP_FAIL;
  }

  if (!auth_validate_session(session_id)) {
    ESP_LOGW(TAG, "Invalid or expired session for %s", req->uri);
    if (strncmp(req->uri, "/api/", 5) == 0) {
      return http_response_json_error(req, HTTPD_401_UNAUTHORIZED,
                                      "Session expired");
    } else {
      httpd_resp_set_status(req, "302 Found");
      httpd_resp_set_hdr(req, "Location", "/login.html");
      httpd_resp_send(req, NULL, 0);
    }
    return ESP_FAIL;
  }

  if (extend_session) {
    auth_extend_session(session_id);
  }

  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  if (!auth_is_password_set()) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/setup.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
  }

  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/html");
  const size_t index_html_size =
      (uintptr_t)index_html_end - (uintptr_t)index_html_start;
  httpd_resp_send(req, (const char *)index_html_start, index_html_size);
  return ESP_OK;
}

static esp_err_t documentation_handler(httpd_req_t *req) {
  if (auth_filter(req, true) != ESP_OK) {
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "text/html");
  const size_t documentation_html_size =
      (uintptr_t)documentation_html_end - (uintptr_t)documentation_html_start;
  httpd_resp_send(req, (const char *)documentation_html_start,
                  documentation_html_size);
  return ESP_OK;
}

static esp_err_t login_handler(httpd_req_t *req) {
  if (!auth_is_password_set()) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/setup.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
  }

  httpd_resp_set_type(req, "text/html");
  const size_t login_html_size =
      (uintptr_t)login_html_end - (uintptr_t)login_html_start;
  httpd_resp_send(req, (const char *)login_html_start, login_html_size);
  return ESP_OK;
}

static esp_err_t setup_handler(httpd_req_t *req) {
  if (auth_is_password_set()) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
  }

  httpd_resp_set_type(req, "text/html");
  const size_t setup_html_size =
      (uintptr_t)setup_html_end - (uintptr_t)setup_html_start;
  httpd_resp_send(req, (const char *)setup_html_start, setup_html_size);
  return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};

static const httpd_uri_t documentation_uri = {.uri = "/documentation.html",
                                              .method = HTTP_GET,
                                              .handler = documentation_handler,
                                              .user_ctx = NULL};

static const httpd_uri_t login_uri = {.uri = "/login.html",
                                      .method = HTTP_GET,
                                      .handler = login_handler,
                                      .user_ctx = NULL};

static const httpd_uri_t setup_uri = {.uri = "/setup.html",
                                      .method = HTTP_GET,
                                      .handler = setup_handler,
                                      .user_ctx = NULL};

static esp_err_t http_redirect_handler(httpd_req_t *req, httpd_err_code_t err
                                       __attribute__((unused))) {
  char host[128] = {0};
  size_t host_len = httpd_req_get_hdr_value_len(req, "Host");

  if (host_len > 0 && host_len < sizeof(host)) {
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
  } else {
    strcpy(host, "192.168.4.1");
  }

  char https_url[700];
  snprintf(https_url, sizeof(https_url), "https://%s%s", host, req->uri);

  httpd_resp_set_status(req, "301 Moved Permanently");
  httpd_resp_set_hdr(req, "Location", https_url);
  httpd_resp_send(req, NULL, 0);

  ESP_LOGI(TAG, "HTTP redirect: %s -> %s", req->uri, https_url);

  return ESP_OK;
}

static esp_err_t load_cert_and_key(char **cert_pem, size_t *cert_size,
                                   char **key_pem, size_t *key_size) {
  esp_err_t err;
  nvs_handle_t nvs_handle;
  err = nvs_open("cert", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS for certificate: %s",
             esp_err_to_name(err));
    return err;
  }

  size_t cert_required_size = 0;
  err = nvs_get_blob(nvs_handle, "cert_pem", NULL, &cert_required_size);
  if (err != ESP_OK) {
    nvs_close(nvs_handle);
    return err;
  }

  size_t key_required_size = 0;
  err = nvs_get_blob(nvs_handle, "key_pem", NULL, &key_required_size);
  if (err != ESP_OK) {
    nvs_close(nvs_handle);
    return err;
  }

  *cert_pem = (char *)malloc(cert_required_size);
  *key_pem = (char *)malloc(key_required_size);

  if (!*cert_pem || !*key_pem) {
    if (*cert_pem)
      free(*cert_pem);
    if (*key_pem)
      free(*key_pem);
    nvs_close(nvs_handle);
    return ESP_ERR_NO_MEM;
  }

  nvs_get_blob(nvs_handle, "cert_pem", *cert_pem, &cert_required_size);
  nvs_get_blob(nvs_handle, "key_pem", *key_pem, &key_required_size);

  nvs_close(nvs_handle);
  *cert_size = cert_required_size;
  *key_size = key_required_size;

  return ESP_OK;
}

void web_server_start(void) {
  esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);
  esp_log_level_set("esp_https_server", ESP_LOG_NONE);
  esp_log_level_set("httpd", ESP_LOG_NONE);

  ESP_LOGI(TAG, "Starting HTTPS server on port 443...");

  char *cert_pem = NULL;
  char *key_pem = NULL;
  size_t cert_size = 0;
  size_t key_size = 0;

  esp_err_t err = load_cert_and_key(&cert_pem, &cert_size, &key_pem, &key_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load certificate and key");
    return;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.max_uri_handlers = 60;
  config.httpd.server_port = 443;
  config.httpd.ctrl_port = 32768;
  config.httpd.keep_alive_enable = true;

  config.servercert = (const uint8_t *)cert_pem;
  config.servercert_len = cert_size;
  config.prvtkey_pem = (const uint8_t *)key_pem;
  config.prvtkey_len = key_size;

  if (httpd_ssl_start(&server, &config) == ESP_OK) {
    free(cert_pem);
    free(key_pem);

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &documentation_uri);
    httpd_register_uri_handler(server, &login_uri);
    httpd_register_uri_handler(server, &setup_uri);

    web_api_register_handlers(server);

    ESP_LOGI(TAG, "HTTPS server started on port 443");

    httpd_config_t redirect_config = HTTPD_DEFAULT_CONFIG();
    redirect_config.server_port = 80;
    redirect_config.ctrl_port = 32769;
    redirect_config.max_uri_handlers = 1;
    redirect_config.keep_alive_enable = true;

    if (httpd_start(&redirect_server, &redirect_config) == ESP_OK) {
      httpd_register_err_handler(redirect_server, HTTPD_404_NOT_FOUND,
                                 http_redirect_handler);
      ESP_LOGI(TAG, "HTTP redirect server started on port 80");
    }
  } else {
    ESP_LOGE(TAG, "Error starting HTTPS server!");
    free(cert_pem);
    free(key_pem);
  }
}

void web_server_stop(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
  if (redirect_server) {
    httpd_stop(redirect_server);
    redirect_server = NULL;
  }
}
