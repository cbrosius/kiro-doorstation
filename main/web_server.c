#include "web_server.h"
#include "wifi_manager.h"
#include "sip_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

static const char* html_page = 
"<!DOCTYPE html>"
"<html><head><title>ESP32 Türstation</title>"
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
"<h1>ESP32 SIP Türstation</h1>"
"<h2>WiFi Konfiguration</h2>"
"<form action='/wifi' method='post'>"
"<div class='form-group'><label>SSID:</label><input type='text' name='ssid' required></div>"
"<div class='form-group'><label>Passwort:</label><input type='password' name='password' required></div>"
"<button type='submit'>WiFi Verbinden</button>"
"</form>"
"<h2>SIP Konfiguration</h2>"
"<form action='/sip' method='post'>"
"<div class='form-group'><label>SIP Server:</label><input type='text' name='server' placeholder='sip.example.com'></div>"
"<div class='form-group'><label>Benutzername:</label><input type='text' name='username' placeholder='doorbell'></div>"
"<div class='form-group'><label>Passwort:</label><input type='password' name='password'></div>"
"<div class='form-group'><label>Apartment 1 URI:</label><input type='text' name='apt1' placeholder='apartment1@example.com'></div>"
"<div class='form-group'><label>Apartment 2 URI:</label><input type='text' name='apt2' placeholder='apartment2@example.com'></div>"
"<button type='submit'>SIP Konfigurieren</button>"
"</form>"
"<h2>DTMF Codes</h2>"
"<p><strong>*1</strong> - Türöffner aktivieren</p>"
"<p><strong>*2</strong> - Licht ein/aus</p>"
"<p><strong>#</strong> - Gespräch beenden</p>"
"</div></body></html>";

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
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
        
        // URL decode (simplified)
        // In production, proper URL decoding should be implemented
        
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
    
    ESP_LOGI(TAG, "SIP Konfiguration empfangen: %s", buf);
    
    // Hier würde die SIP-Konfiguration geparst und gespeichert
    sip_save_config(buf); // Vereinfacht
    
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