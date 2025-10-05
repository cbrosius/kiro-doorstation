#include "web_server.h"
#include "wifi_manager.h"
#include "sip_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

static char* url_decode(char *str) {
    char *p = str;
    char *q = str;
    while (*p) {
        if (*p == '%') {
            if (p[1] && p[2]) {
                char hex[3] = {p[1], p[2], 0};
                *q++ = (char)strtol(hex, NULL, 16);
                p += 3;
            } else {
                *q++ = *p++;
            }
        } else if (*p == '+') {
            *q++ = ' ';
            p++;
        } else {
            *q++ = *p++;
        }
    }
    *q = 0;
    return str;
}

static void trim_whitespace(char *str) {
    char *end;
    // Trim leading space
    while (*str == ' ') str++;
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && *end == ' ') end--;
    *(end + 1) = 0;
}

static bool validate_ssid(const char *ssid) {
    size_t len = strlen(ssid);
    return len > 0 && len < 32;
}

static bool validate_password(const char *password) {
    size_t len = strlen(password);
    return len > 0 && len < 64;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_type(req, "image/x-icon");
    return ESP_OK;
}

static const char* get_html_page(bool is_connected) {
    if (!is_connected) {
        // AP mode: only WiFi config
        return
        "<!DOCTYPE html>"
        "<html><head><title>ESP32 Door Station Setup</title>"
        "<meta charset='UTF-8'>"
        "<style>body{font-family:Arial;margin:40px;background:#f0f0f0}"
        ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;text-align:center}"
        ".form-group{margin:15px 0}"
        "label{display:block;margin-bottom:5px;font-weight:bold}"
        "input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}"
        "button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}"
        "button:hover{background:#0056b3}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>ESP32 Door Station Setup</h1>"
        "<p>Please connect to your WiFi network:</p>"
        "<form action='/wifi' method='post'>"
        "<div class='form-group'><label>WiFi SSID:</label><input type='text' name='ssid' required></div>"
        "<div class='form-group'><label>WiFi Password:</label><input type='password' name='password' required></div>"
        "<button type='submit'>Connect to WiFi</button>"
        "</form>"
        "</div></body></html>";
    } else {
        // Connected mode: full config
        return
        "<!DOCTYPE html>"
        "<html><head><title>ESP32 Door Station</title>"
        "<meta charset='UTF-8'>"
        "<style>body{font-family:Arial;margin:40px;background:#f0f0f0}"
        ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;text-align:center}"
        ".form-group{margin:15px 0}"
        "label{display:block;margin-bottom:5px;font-weight:bold}"
        "input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}"
        "button{background:#007bff;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}"
        "button:hover{background:#0056b3}"
        ".status{padding:10px;margin:10px 0;border-radius:5px}"
        ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}"
        ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>ESP32 Door Station</h1>"
        "<div class='status success'>✓ Connected to WiFi</div>"
        "<h2>SIP Configuration</h2>"
        "<form action='/sip' method='post'>"
        "<div class='form-group'><label>SIP Server:</label><input type='text' name='server' placeholder='sip.example.com' required></div>"
        "<div class='form-group'><label>Username:</label><input type='text' name='username' placeholder='doorbell' required></div>"
        "<div class='form-group'><label>Password:</label><input type='password' name='password'></div>"
        "<div class='form-group'><label>Apartment 1 URI:</label><input type='text' name='apt1' placeholder='apartment1@example.com' required></div>"
        "<div class='form-group'><label>Apartment 2 URI:</label><input type='text' name='apt2' placeholder='apartment2@example.com' required></div>"
        "<button type='submit'>Save SIP Config</button>"
        "</form>"
        "<h2>DTMF Codes</h2>"
        "<p><strong>*1</strong> - Activate door opener</p>"
        "<p><strong>*2</strong> - Toggle light</p>"
        "<p><strong>#</strong> - End call</p>"
        "</div></body></html>";
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    bool is_connected = wifi_is_connected();
    const char* page = get_html_page(is_connected);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, strlen(page));
    return ESP_OK;
}

static esp_err_t captive_portal_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    char buf[1000];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse form data
    char ssid[64] = {0};
    char password[64] = {0};
    
    char *ssid_start = strstr(buf, "ssid=");
    char *password_start = strstr(buf, "password=");
    
    if (ssid_start && password_start) {
        ssid_start += 5; // Skip "ssid="
        char *ssid_end = strchr(ssid_start, '&');
        if (ssid_end) {
            strncpy(ssid, ssid_start, ssid_end - ssid_start);
        }
        
        password_start += 9; // Skip "password="
        char *password_end = strchr(password_start, '&');
        if (password_end) {
            strncpy(password, password_start, password_end - password_start);
        } else {
            strcpy(password, password_start);
        }

        // URL decode
        url_decode(ssid);
        url_decode(password);

        // Trim whitespace
        trim_whitespace(ssid);
        trim_whitespace(password);

        // Validate
        if (!validate_ssid(ssid) || !validate_password(password)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "WiFi Konfiguration: SSID=%s", ssid);
        wifi_save_config(ssid, password);
        wifi_connect_sta(ssid, password);
        
        const char* response = "<html><body><h1>WiFi Konfiguration gespeichert</h1>"
                              "<p>Das Gerät versucht sich zu verbinden...</p>"
                              "<a href='/'>Zurück</a></body></html>";
        httpd_resp_send(req, response, strlen(response));
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
    }
    
    return ESP_OK;
}

static esp_err_t sip_handler(httpd_req_t *req)
{
    char buf[1000];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse SIP config (simplified)
    char server[64] = {0};
    char username[64] = {0};
    char sip_password[64] = {0};
    char apt1[128] = {0};
    char apt2[128] = {0};

    char *server_start = strstr(buf, "server=");
    char *username_start = strstr(buf, "username=");
    char *password_start = strstr(buf, "password=");
    char *apt1_start = strstr(buf, "apt1=");
    char *apt2_start = strstr(buf, "apt2=");

    if (server_start) {
        server_start += 7;
        char *end = strchr(server_start, '&');
        if (end) strncpy(server, server_start, end - server_start);
        else strcpy(server, server_start);
        url_decode(server);
        trim_whitespace(server);
    }

    if (username_start) {
        username_start += 9;
        char *end = strchr(username_start, '&');
        if (end) strncpy(username, username_start, end - username_start);
        else strcpy(username, username_start);
        url_decode(username);
        trim_whitespace(username);
    }

    if (password_start) {
        password_start += 9;
        char *end = strchr(password_start, '&');
        if (end) strncpy(sip_password, password_start, end - password_start);
        else strcpy(sip_password, password_start);
        url_decode(sip_password);
        trim_whitespace(sip_password);
    }

    if (apt1_start) {
        apt1_start += 5;
        char *end = strchr(apt1_start, '&');
        if (end) strncpy(apt1, apt1_start, end - apt1_start);
        else strcpy(apt1, apt1_start);
        url_decode(apt1);
        trim_whitespace(apt1);
    }

    if (apt2_start) {
        apt2_start += 5;
        char *end = strchr(apt2_start, '&');
        if (end) strncpy(apt2, apt2_start, end - apt2_start);
        else strcpy(apt2, apt2_start);
        url_decode(apt2);
        trim_whitespace(apt2);
    }

    // Validate
    if (strlen(server) == 0 || strlen(username) == 0 || strlen(apt1) == 0 || strlen(apt2) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required SIP fields");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SIP Konfiguration: Server=%s, User=%s", server, username);

    // Save SIP config (placeholder)
    // sip_save_config(server, username, sip_password, apt1, apt2);

    const char* response = "<html><body><h1>SIP Konfiguration gespeichert</h1>"
                          "<a href='/'>Zurück</a></body></html>";
    httpd_resp_send(req, response, strlen(response));
    
    return ESP_OK;
}

void web_server_start(void)
{
    ESP_LOGI(TAG, "Web Server starten");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t wifi_uri = {
            .uri = "/wifi",
            .method = HTTP_POST,
            .handler = wifi_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &wifi_uri);
        
        httpd_uri_t sip_uri = {
            .uri = "/sip",
            .method = HTTP_POST,
            .handler = sip_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &sip_uri);

        // Captive portal handlers
        httpd_uri_t generate_204_uri = {
            .uri = "/generate_204",
            .method = HTTP_GET,
            .handler = captive_portal_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &generate_204_uri);

        httpd_uri_t hotspot_detect_uri = {
            .uri = "/hotspot-detect.html",
            .method = HTTP_GET,
            .handler = captive_portal_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &hotspot_detect_uri);

        httpd_uri_t success_uri = {
            .uri = "/library/test/success.html",
            .method = HTTP_GET,
            .handler = captive_portal_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &success_uri);

        httpd_uri_t ncsi_uri = {
            .uri = "/ncsi.txt",
            .method = HTTP_GET,
            .handler = captive_portal_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ncsi_uri);

        httpd_uri_t favicon_uri = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &favicon_uri);

        ESP_LOGI(TAG, "Web Server gestartet auf Port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Fehler beim Starten des Web Servers");
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web Server gestoppt");
    }
}