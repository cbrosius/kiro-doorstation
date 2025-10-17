#pragma GCC diagnostic ignored "-Waddress"
#include "web_server.h"
#include "web_api.h"
#include "cert_manager.h"
#include "auth_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;
static httpd_handle_t redirect_server = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t documentation_html_start[] asm("_binary_documentation_html_start");
extern const uint8_t documentation_html_end[] asm("_binary_documentation_html_end");
extern const uint8_t login_html_start[] asm("_binary_login_html_start");
extern const uint8_t login_html_end[] asm("_binary_login_html_end");
extern const uint8_t setup_html_start[] asm("_binary_setup_html_start");
extern const uint8_t setup_html_end[] asm("_binary_setup_html_end");

/**
 * @brief Check if a URI is a public endpoint that doesn't require authentication
 * 
 * @param uri The URI to check
 * @return true if the endpoint is public
 * @return false if the endpoint requires authentication
 */
static bool is_public_endpoint(const char* uri) {
    if (!uri) {
        return false;
    }
    
    // Public endpoints that don't require authentication
    const char* public_endpoints[] = {
        "/api/auth/login",           // Login endpoint
        "/api/auth/set-password",    // Initial password setup
        "/login.html",               // Login page
        "/setup.html",               // Setup page
        "/favicon.ico",              // Browser favicon requests
        NULL
    };
    
    for (int i = 0; public_endpoints[i] != NULL; i++) {
        if (strcmp(uri, public_endpoints[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Authentication filter for HTTP requests
 * 
 * This filter checks for a valid session cookie on all protected endpoints.
 * If the session is missing or invalid, it returns 401 Unauthorized.
 * If the session is valid, it extends the session timeout.
 * 
 * @param req HTTP request
 * @return ESP_OK if authentication passed, ESP_FAIL if authentication failed
 */
esp_err_t auth_filter(httpd_req_t *req) {
    // Skip authentication for public endpoints
    if (is_public_endpoint(req->uri)) {
        return ESP_OK;
    }

    // Check if password is set - if not, redirect to setup page
    if (!auth_is_password_set()) {
        ESP_LOGW(TAG, "No password set - redirecting to setup page");

        // Check if this is an API request or HTML page request
        if (strncmp(req->uri, "/api/", 5) == 0) {
            // API request - return JSON error
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Initial setup required\"}", -1);
        } else {
            // HTML page request - redirect to setup page
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/setup.html");
            httpd_resp_send(req, NULL, 0);
        }
        return ESP_FAIL;
    }
    
    // Extract session cookie
    char session_id[AUTH_SESSION_ID_SIZE] = {0};
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    
    if (cookie_len > 0) {
        char* cookie_str = malloc(cookie_len + 1);
        if (cookie_str) {
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_str, cookie_len + 1) == ESP_OK) {
                // Parse session_id from cookie string
                // Cookie format: "session_id=<value>; other=value"
                const char* session_start = strstr(cookie_str, "session_id=");
                if (session_start) {
                    session_start += strlen("session_id=");
                    const char* session_end = strchr(session_start, ';');
                    size_t session_len = session_end ? (size_t)(session_end - session_start) : strlen(session_start);
                    
                    if (session_len < AUTH_SESSION_ID_SIZE) {
                        strncpy(session_id, session_start, session_len);
                        session_id[session_len] = '\0';
                    }
                }
            }
            free(cookie_str);
        }
    }
    
    // Check if session ID was found
    if (session_id[0] == '\0') {
        ESP_LOGW(TAG, "No session cookie found for %s", req->uri);

        // Check if this is an API request or HTML page request
        if (req->uri[0] != '\0' && memcmp(req->uri, "/api/", 5) == 0) {
            // API request - return JSON error
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Authentication required\"}", -1);
        } else {
            // HTML page request - redirect to login page
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/login.html");
            httpd_resp_send(req, NULL, 0);
        }
        return ESP_FAIL;
    }
    
    // Validate session
    if (!auth_validate_session(session_id)) {
        ESP_LOGW(TAG, "Invalid or expired session for %s", req->uri);

        // Check if this is an API request or HTML page request
        if (req->uri[0] != '\0' && memcmp(req->uri, "/api/", 5) == 0) {
            // API request - return JSON error
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Session expired\"}", -1);
        } else {
            // HTML page request - redirect to login page
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/login.html");
            httpd_resp_send(req, NULL, 0);
        }
        return ESP_FAIL;
    }
    
    // Session is valid - extend timeout on activity
    auth_extend_session(session_id);
    
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    // If password is not set, redirect to setup page
    if (!auth_is_password_set()) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/setup.html");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    const size_t index_html_size = (uintptr_t)index_html_end - (uintptr_t)index_html_start;
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static esp_err_t documentation_handler(httpd_req_t *req)
{
    // Check authentication
    if (auth_filter(req) != ESP_OK) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    const size_t documentation_html_size = (uintptr_t)documentation_html_end - (uintptr_t)documentation_html_start;
    httpd_resp_send(req, (const char *)documentation_html_start, documentation_html_size);
    return ESP_OK;
}

static esp_err_t login_handler(httpd_req_t *req)
{
    // If password is not set, redirect to setup page
    if (!auth_is_password_set()) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/setup.html");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // Login page is public - no authentication required
    httpd_resp_set_type(req, "text/html");
    const size_t login_html_size = (uintptr_t)login_html_end - (uintptr_t)login_html_start;
    httpd_resp_send(req, (const char *)login_html_start, login_html_size);
    return ESP_OK;
}

static esp_err_t setup_handler(httpd_req_t *req)
{
    // If password is already set, redirect to login page
    if (auth_is_password_set()) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // Setup page is public - no authentication required
    httpd_resp_set_type(req, "text/html");
    const size_t setup_html_size = (uintptr_t)setup_html_end - (uintptr_t)setup_html_start;
    httpd_resp_send(req, (const char *)setup_html_start, setup_html_size);
    return ESP_OK;
}


// URI handlers for HTML pages
static const httpd_uri_t root_uri = {
    .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL
};

static const httpd_uri_t documentation_uri = {
    .uri = "/documentation.html", .method = HTTP_GET, .handler = documentation_handler, .user_ctx = NULL
};

static const httpd_uri_t login_uri = {
    .uri = "/login.html", .method = HTTP_GET, .handler = login_handler, .user_ctx = NULL
};

static const httpd_uri_t setup_uri = {
    .uri = "/setup.html", .method = HTTP_GET, .handler = setup_handler, .user_ctx = NULL
};


// HTTP to HTTPS redirect handler (used as error handler for 404)
static esp_err_t http_redirect_handler(httpd_req_t *req, httpd_err_code_t err __attribute__((unused)))
{
    // Get the Host header to construct the HTTPS URL
    char host[128] = {0};
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    
    if (host_len > 0 && host_len < sizeof(host)) {
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    } else {
        // Fallback to IP address if Host header is missing
        strcpy(host, "192.168.4.1");  // Default AP IP
    }
    
    // Construct HTTPS URL with sufficient buffer size
    // Max: "https://" (8) + host (127) + uri (512) + null (1) = 648 bytes
    char https_url[700];
    snprintf(https_url, sizeof(https_url), "https://%s%s", host, req->uri);
    
    // Send 301 Moved Permanently redirect
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", https_url);
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "HTTP redirect: %s -> %s", req->uri, https_url);
    
    return ESP_OK;
}

// Helper function to load certificate and key from NVS
static esp_err_t load_cert_and_key(char** cert_pem, size_t* cert_size, char** key_pem, size_t* key_size)
{
    esp_err_t err;
    
    // Open NVS
    nvs_handle_t nvs_handle;
    err = nvs_open("cert", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for certificate: %s", esp_err_to_name(err));
        return err;
    }
    
    // Get certificate size
    size_t cert_required_size = 0;
    err = nvs_get_blob(nvs_handle, "cert_pem", NULL, &cert_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Get key size
    size_t key_required_size = 0;
    err = nvs_get_blob(nvs_handle, "key_pem", NULL, &key_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get key size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Allocate memory for certificate
    *cert_pem = (char*)malloc(cert_required_size);
    if (!*cert_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificate");
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate memory for key
    *key_pem = (char*)malloc(key_required_size);
    if (!*key_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for key");
        free(*cert_pem);
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Read certificate
    err = nvs_get_blob(nvs_handle, "cert_pem", *cert_pem, &cert_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
        free(*cert_pem);
        free(*key_pem);
        nvs_close(nvs_handle);
        return err;
    }
    
    // Read key
    err = nvs_get_blob(nvs_handle, "key_pem", *key_pem, &key_required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read key: %s", esp_err_to_name(err));
        free(*cert_pem);
        free(*key_pem);
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    *cert_size = cert_required_size;
    *key_size = key_required_size;
    
    ESP_LOGI(TAG, "Certificate and key loaded from NVS (cert: %d bytes, key: %d bytes)", 
             cert_required_size, key_required_size);
    
    return ESP_OK;
}

void web_server_start(void)
{
    // Reduce log verbosity for TLS handshake errors (expected with self-signed certs)
    // Browsers will reject self-signed certificates, causing repeated handshake failures
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);     // Suppress TLS handshake errors
    esp_log_level_set("esp_https_server", ESP_LOG_NONE);    // Suppress HTTPS server session errors
    esp_log_level_set("httpd", ESP_LOG_NONE);               // Suppress HTTP daemon connection errors
    
    // Log that we've started (since we suppressed the component logs)
    ESP_LOGI(TAG, "Starting HTTPS server on port 443...");
    
    // Load certificate and key from NVS (certificate should already be initialized in main.c)
    char* cert_pem = NULL;
    char* key_pem = NULL;
    size_t cert_size = 0;
    size_t key_size = 0;
    
    esp_err_t err = load_cert_and_key(&cert_pem, &cert_size, &key_pem, &key_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load certificate and key");
        return;
    }
    
    // Configure HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.max_uri_handlers = 60;  // Increased for auth, certificate, and JS file endpoints
    config.httpd.server_port = 443;
    config.httpd.ctrl_port = 32768;
    
    // Set certificate and key
    config.servercert = (const uint8_t*)cert_pem;
    config.servercert_len = cert_size;
    config.prvtkey_pem = (const uint8_t*)key_pem;
    config.prvtkey_len = key_size;
    
    // Configure TLS 1.2 as minimum (this is typically the default)
    // The mbedtls configuration handles the TLS version and cipher suites
    
    if (httpd_ssl_start(&server, &config) == ESP_OK) {
        // Free the allocated memory after server starts (it makes internal copies)
        free(cert_pem);
        free(key_pem);
        
        // Register HTML page handlers
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &documentation_uri);
        httpd_register_uri_handler(server, &login_uri);
        httpd_register_uri_handler(server, &setup_uri);
        
        
        // Register all API handlers via the API module
        web_api_register_handlers(server);
        
        ESP_LOGI(TAG, "HTTPS server started on port 443 with all endpoints");
        
        // Start HTTP redirect server on port 80
        httpd_config_t redirect_config = HTTPD_DEFAULT_CONFIG();
        redirect_config.server_port = 80;
        redirect_config.ctrl_port = 32769;  // Different control port
        redirect_config.max_uri_handlers = 1;  // Need at least 1 for error handler
        
        esp_err_t redirect_err = httpd_start(&redirect_server, &redirect_config);
        if (redirect_err == ESP_OK) {
            // Register error handler for 404 to redirect all requests to HTTPS
            httpd_register_err_handler(redirect_server, HTTPD_404_NOT_FOUND, http_redirect_handler);
            ESP_LOGI(TAG, "HTTP redirect server started on port 80");
        } else {
            ESP_LOGW(TAG, "Failed to start HTTP redirect server on port 80: %s", esp_err_to_name(redirect_err));
            ESP_LOGW(TAG, "HTTP to HTTPS redirect will not be available");
        }
    } else {
        ESP_LOGE(TAG, "Error starting HTTPS server!");
        free(cert_pem);
        free(key_pem);
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTPS server stopped");
    }
    
    if (redirect_server) {
        httpd_stop(redirect_server);
        redirect_server = NULL;
        ESP_LOGI(TAG, "HTTP redirect server stopped");
    }
}
