#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "sip_client.h"
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
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<style>body{font-family:Arial;margin:20px;background:#f0f0f0}"
        ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
        "h1{color:#333;text-align:center;font-size:24px}"
        ".form-group{margin:15px 0}"
        "label{display:block;margin-bottom:5px;font-weight:bold}"
        "input[type=text],input[type=password],select{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px}"
        "button{background:#007bff;color:white;padding:12px 24px;border:none;border-radius:5px;cursor:pointer;margin:5px;font-size:16px;width:100%;max-width:200px}"
        "button:hover{background:#0056b3}"
        "@media (max-width: 600px) {body{margin:10px}.container{padding:15px}h1{font-size:20px}button{padding:14px}}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>ESP32 Door Station Setup</h1>"
        "<p>Welcome! This is the setup page for your ESP32 Door Station. Connect your device to a WiFi network to enable SIP calling and door control features. Use the scan button to find available networks, or enter your network details manually.</p>"
        "<p>Please connect to your WiFi network:</p>"
        "<button onclick='triggerNewScan()' style='margin-bottom:10px;' id='new-scan-button'>Scan for Networks</button>"
        "<p id='scan-status' style='margin-bottom:10px;'></p>"
        "<form action='/wifi' method='post'>"
        "<div class='form-group'><label>WiFi SSID:</label><select name='ssid' id='ssid-select' onchange='document.getElementById(\"ssid_manual\").value = this.value;'><option value=''>Scan networks or enter SSID manually</option></select><br><input type='text' id='ssid_manual' name='ssid_manual' placeholder='Or enter manually' required style='margin-top:5px;width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px;'></div>"
        "<div class='form-group'><label>WiFi Password:</label><input type='password' name='password' required></div>"
        "<button type='submit'>Connect to WiFi</button>"
        "</form>"
        "<script>"
        "function triggerNewScan() {"
        "  console.log('Scan button clicked');"
        "  var status = document.getElementById('scan-status');"
        "  var button = document.getElementById('new-scan-button');"
        "  var select = document.getElementById('ssid-select');"
        "  if (!status || !button || !select) {"
        "    console.error('Elements not found');"
        "    return;"
        "  }"
        "  status.textContent = 'Scanning for networks...';"
        "  button.textContent = 'Scanning...';"
        "  button.disabled = true;"
        "  fetch('/scan?new=1')"
        "    .then(function(response) {"
        "      console.log('Scan response:', response.status);"
        "      if (!response.ok) {"
        "        throw new Error('HTTP ' + response.status);"
        "      }"
        "      return response.json();"
        "    })"
        "    .then(function(data) {"
        "      console.log('Scan data:', data);"
        "      while (select.children.length > 0) {"
        "        select.removeChild(select.lastChild);"
        "      }"
        "      if (data && data.length > 0) {"
        "        for (var i = 0; i < data.length; i++) {"
        "          var option = document.createElement('option');"
        "          option.value = data[i];"
        "          option.textContent = data[i];"
        "          select.appendChild(option);"
        "          console.log('Added network:', data[i]);"
        "        }"
        "        status.textContent = 'Found ' + data.length + ' networks';"
        "        status.style.color = 'green';"
        "      } else {"
        "        status.textContent = 'No networks found';"
        "        status.style.color = 'orange';"
        "      }"
        "    })"
        "    .catch(function(error) {"
        "      console.error('Scan error:', error);"
        "      status.textContent = 'Scan error';"
        "      status.style.color = 'red';"
        "    })"
        "    .then(function() {"
        "      button.textContent = 'Scan for Networks';"
        "      button.disabled = false;"
        "    });"
        "}"
        "</script>"
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
        "<h2>SIP Status</h2>"
        "<div id='sip-status' class='status' style='margin-bottom:20px;'>Loading SIP status...</div>"
        "<button onclick='refreshSipStatus()' style='margin-bottom:20px;'>Refresh SIP Status</button>"
        "<h2>SIP Configuration</h2>"
        "<div id='sip-save-result' class='status' style='display:none;margin-bottom:15px;'></div>"
        "<div id='current-sip-config' style='margin-bottom:15px;padding:10px;background:#e9ecef;border-radius:5px;display:none;'></div>"
        "<form action='/sip' method='post' onsubmit='return handleSipConfigSubmit()'>"
        "<div class='form-group'><label>SIP Server:</label><input type='text' name='server' id='sip-server' placeholder='sip.example.com' required></div>"
        "<div class='form-group'><label>Username:</label><input type='text' name='username' id='sip-username' placeholder='doorbell' required></div>"
        "<div class='form-group'><label>Password:</label><input type='password' name='password' id='sip-password'></div>"
        "<div class='form-group'><label>Apartment 1 URI:</label><input type='text' name='apt1' id='sip-apt1' placeholder='apartment1@example.com' required></div>"
        "<div class='form-group'><label>Apartment 2 URI:</label><input type='text' name='apt2' id='sip-apt2' placeholder='apartment2@example.com' required></div>"
        "<button type='submit' id='sip-save-btn'>Save SIP Config</button>"
        "</form>"
        "<button onclick='testSipConnection()' style='background:#17a2b8;color:white;margin-top:10px;'>Test SIP Connection</button>"
        "<div id='sip-test-result' class='status' style='display:none;margin-top:10px;'></div>"
        "<h2>System</h2>"
        "<form action='/reset' method='post' onsubmit='return confirm(\"Are you sure you want to reset to factory defaults?\")'>"
        "<button type='submit' style='background:#dc3545;color:white;'>Factory Reset</button>"
        "</form>"
        "<h2>DTMF Codes</h2>"
        "<p><strong>*1</strong> - Activate door opener</p>"
        "<p><strong>*2</strong> - Toggle light</p>"
        "<p><strong>#</strong> - End call</p>"
        "<script>"
        "function handleSipConfigSubmit() {"
        "  var saveBtn = document.getElementById('sip-save-btn');"
        "  var resultDiv = document.getElementById('sip-save-result');"
        "  if (!saveBtn || !resultDiv) return true;"
        "  "
        "  // Disable button and show saving message"
        "  saveBtn.textContent = 'Saving...';"
        "  saveBtn.disabled = true;"
        "  "
        "  resultDiv.textContent = 'Saving SIP configuration...';"
        "  resultDiv.className = 'status';"
        "  resultDiv.style.display = 'block';"
        "  "
        "  // Submit form via fetch to handle response"
        "  var form = saveBtn.closest('form');"
        "  var formData = new FormData(form);"
        "  "
        "  fetch('/sip', {"
        "    method: 'POST',"
        "    body: formData"
        "  })"
        "  .then(function(response) {"
        "    if (!response.ok) {"
        "      throw new Error('HTTP ' + response.status);"
        "    }"
        "    return response.text();"
        "  })"
        "  .then(function(html) {"
        "    resultDiv.textContent = '✓ SIP configuration saved successfully';"
        "    resultDiv.className = 'status success';"
        "    "
        "    // Reload current configuration after a short delay"
        "    setTimeout(function() {"
        "      loadCurrentSipConfig();"
        "    }, 1000);"
        "  })"
        "  .catch(function(error) {"
        "    console.error('SIP save error:', error);"
        "    resultDiv.textContent = '✗ Failed to save SIP configuration';"
        "    resultDiv.className = 'status error';"
        "  })"
        "  .then(function() {"
        "    // Re-enable button"
        "    saveBtn.textContent = 'Save SIP Config';"
        "    saveBtn.disabled = false;"
        "  });"
        "  "
        "  // Prevent actual form submission"
        "  return false;"
        "}"
        ""
        "function testSipConnection() {"
        "  var resultDiv = document.getElementById('sip-test-result');"
        "  if (!resultDiv) return;"
        "  "
        "  resultDiv.textContent = 'Testing SIP connection...';"
        "  resultDiv.className = 'status';"
        "  resultDiv.style.display = 'block';"
        "  "
        "  fetch('/sip-test')"
        "    .then(function(response) {"
        "      if (!response.ok) {"
        "        throw new Error('HTTP ' + response.status);"
        "      }"
        "      return response.json();"
        "    })"
        "    .then(function(data) {"
        "      if (data.success === true) {"
        "        resultDiv.textContent = '✓ ' + data.message;"
        "        resultDiv.className = 'status success';"
        "      } else {"
        "        resultDiv.textContent = '✗ ' + data.message;"
        "        resultDiv.className = 'status error';"
        "      }"
        "    })"
        "    .catch(function(error) {"
        "      console.error('SIP test error:', error);"
        "      resultDiv.textContent = '✗ SIP test failed';"
        "      resultDiv.className = 'status error';"
        "    });"
        "}"
        ""
        "function refreshSipStatus() {"
        "  var statusDiv = document.getElementById('sip-status');"
        "  if (!statusDiv) return;"
        "  "
        "  fetch('/sip-status')"
        "    .then(function(response) {"
        "      if (!response.ok) {"
        "        throw new Error('HTTP ' + response.status);"
        "      }"
        "      return response.json();"
        "    })"
        "    .then(function(data) {"
        "      var statusText = 'SIP State: ' + data.state;"
        "      if (data.configured === true) {"
        "        statusText += ' | Server: ' + data.server + ' | User: ' + data.username;"
        "      } else {"
        "        statusText += ' | Not configured';"
        "      }"
        "      "
        "      statusDiv.textContent = statusText;"
        "      "
        "      // Set status color based on state"
        "      switch(data.state) {"
        "        case 'CONNECTED':"
        "          statusDiv.className = 'status success';"
        "          break;"
        "        case 'IDLE':"
        "          statusDiv.className = 'status success';"
        "          break;"
        "        case 'REGISTERING':"
        "          statusDiv.className = 'status';"
        "          break;"
        "        case 'ERROR':"
        "          statusDiv.className = 'status error';"
        "          break;"
        "        default:"
        "          statusDiv.className = 'status';"
        "      }"
        "    })"
        "    .catch(function(error) {"
        "      console.error('SIP status error:', error);"
        "      statusDiv.textContent = '✗ Failed to load SIP status';"
        "      statusDiv.className = 'status error';"
        "    });"
        "}"
        ""
        "// Auto-refresh SIP status every 5 seconds"
        "setInterval(refreshSipStatus, 5000);"
        ""
        "function loadCurrentSipConfig() {"
        "  fetch('/sip-status')"
        "    .then(function(response) {"
        "      if (!response.ok) {"
        "        throw new Error('HTTP ' + response.status);"
        "      }"
        "      return response.json();"
        "    })"
        "    .then(function(data) {"
        "      if (data.configured === true) {"
        "        // Pre-populate form fields"
        "        document.getElementById('sip-server').value = data.server || '';"
        "        document.getElementById('sip-username').value = data.username || '';"
        "        document.getElementById('sip-apt1').value = data.apartment1 || '';"
        "        document.getElementById('sip-apt2').value = data.apartment2 || '';"
        "        "
        "        // Show current configuration"
        "        var configDiv = document.getElementById('current-sip-config');"
        "        if (configDiv) {"
        "          configDiv.innerHTML = '<strong>Current Configuration:</strong><br>' +"
        "            'Server: ' + data.server + '<br>' +"
        "            'Username: ' + data.username + '<br>' +"
        "            'Apartment 1: ' + data.apartment1 + '<br>' +"
        "            'Apartment 2: ' + data.apartment2 + '<br>' +"
        "            'Port: ' + data.port;"
        "          configDiv.style.display = 'block';"
        "        }"
        "      }"
        "    })"
        "    .catch(function(error) {"
        "      console.error('Load SIP config error:', error);"
        "    });"
        "}"
        ""
        "// Initial status load"
        "document.addEventListener('DOMContentLoaded', function() {"
        "  refreshSipStatus();"
        "  loadCurrentSipConfig();"
        "});"
        "</script>"
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
    char *ssid_manual_start = strstr(buf, "ssid_manual=");
    char *password_start = strstr(buf, "password=");

    if (password_start) {
        password_start += 9; // Skip "password="
        char *password_end = strchr(password_start, '&');
        if (password_end) {
            strncpy(password, password_start, password_end - password_start);
        } else {
            strcpy(password, password_start);
        }

        // Get SSID from select or manual
        if (ssid_start) {
            ssid_start += 5; // Skip "ssid="
            char *ssid_end = strchr(ssid_start, '&');
            if (ssid_end) {
                strncpy(ssid, ssid_start, ssid_end - ssid_start);
            }
        }
        if (strlen(ssid) == 0 && ssid_manual_start) {
            ssid_manual_start += 12; // Skip "ssid_manual="
            char *ssid_end = strchr(ssid_manual_start, '&');
            if (ssid_end) {
                strncpy(ssid, ssid_manual_start, ssid_end - ssid_manual_start);
            } else {
                strcpy(ssid, ssid_manual_start);
            }
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

        ESP_LOGI(TAG, "WiFi Configuration: SSID=%s", ssid);
        wifi_save_config(ssid, password);

        const char* response = "<html><body><h1>WiFi Configuration Saved</h1>"
                              "<p>The device will restart and connect...</p></body></html>";
        httpd_resp_send(req, response, strlen(response));

        // Restart to apply new WiFi config
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
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
        const char* error_response = "<html><body><h1>Error</h1>"
                                   "<p style='color:red;'>Missing required SIP fields. Please fill in all required fields.</p>"
                                   "<a href='/'>Back</a></body></html>";
        httpd_resp_send(req, error_response, strlen(error_response));
        return ESP_OK;
    }

    // Validate server format (basic check)
    if (strstr(server, "sip.") == NULL && strstr(server, ".") == NULL) {
        const char* error_response = "<html><body><h1>Error</h1>"
                                   "<p style='color:red;'>Invalid SIP server format. Please enter a valid SIP server address.</p>"
                                   "<a href='/'>Back</a></body></html>";
        httpd_resp_send(req, error_response, strlen(error_response));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "SIP Configuration: Server=%s, User=%s", server, username);

    // Save SIP config
    sip_save_config(server, username, sip_password, apt1, apt2, 5060);

    const char* response = "<html><body><h1>SIP Configuration Saved</h1>"
                           "<p style='color:green;'>Configuration saved successfully!</p>"
                           "<a href='/'>Back</a></body></html>";
    httpd_resp_send(req, response, strlen(response));
    
    return ESP_OK;
}

static esp_err_t sip_test_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "SIP Test angefordert");

    bool test_result = sip_client_test_connection();

    const char* response_template =
        "{"
        "\"success\": %s,"
        "\"message\": \"%s\","
        "\"timestamp\": \"%lld\""
        "}";

    char* message;
    if (test_result) {
        message = "SIP Verbindung erfolgreich";
    } else {
        // Get current state for more detailed error message
        sip_state_t current_state = sip_client_get_state();
        switch (current_state) {
            case SIP_STATE_ERROR:
                message = "SIP Fehler - Bitte überprüfen Sie die Konfiguration";
                break;
            case SIP_STATE_DISCONNECTED:
                message = "SIP nicht verbunden - Konfiguration erforderlich";
                break;
            case SIP_STATE_REGISTERING:
                message = "SIP Registrierung läuft - Bitte warten";
                break;
            default:
                message = "SIP Verbindung fehlgeschlagen";
        }
    }

    char response[256];
    snprintf(response, sizeof(response), response_template,
             test_result ? "true" : "false",
             message,
             esp_timer_get_time() / 1000);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}

static esp_err_t sip_status_handler(httpd_req_t *req)
{
    char status_buffer[512];

    // Clear buffer first
    memset(status_buffer, 0, sizeof(status_buffer));

    sip_get_status(status_buffer, sizeof(status_buffer));

    // Check if we got valid data
    if (strlen(status_buffer) == 0) {
        const char* error_response =
            "{"
            "\"state\": \"ERROR\","
            "\"state_code\": 8,"
            "\"configured\": false,"
            "\"server\": \"\","
            "\"username\": \"\","
            "\"apartment1\": \"\","
            "\"apartment2\": \"\","
            "\"port\": 0"
            "}";

        strcpy(status_buffer, error_response);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, status_buffer, strlen(status_buffer));

    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested");

    // Clear WiFi config
    wifi_clear_config();

    const char* response = "<html><body><h1>Factory Reset Complete</h1>"
                          "<p>The device will restart...</p></body></html>";
    httpd_resp_send(req, response, strlen(response));

    // Restart after a short delay
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    // Check for new scan parameter
    char buf[32];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));

    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "new", param, sizeof(param)) == ESP_OK) {
            ESP_LOGI(TAG, "New scan requested, clearing results");
            wifi_clear_scan_results();
            wifi_start_background_scan();
            // Wait a bit for scan to complete
            vTaskDelay(pdMS_TO_TICKS(4000));
        }
    }

    char *json = wifi_scan_networks();
    if (json == NULL) {
        ESP_LOGE(TAG, "Scan returned NULL");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Scan result: %s", json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

void web_server_start(void)
{
    ESP_LOGI(TAG, "Starting web server");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    
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

        httpd_uri_t scan_uri = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);

        httpd_uri_t sip_uri = {
            .uri = "/sip",
            .method = HTTP_POST,
            .handler = sip_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &sip_uri);

        httpd_uri_t sip_test_uri = {
            .uri = "/sip-test",
            .method = HTTP_GET,
            .handler = sip_test_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &sip_test_uri);

        httpd_uri_t sip_status_uri = {
            .uri = "/sip-status",
            .method = HTTP_GET,
            .handler = sip_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &sip_status_uri);

        httpd_uri_t reset_uri = {
            .uri = "/reset",
            .method = HTTP_POST,
            .handler = reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &reset_uri);

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

        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Error starting web server");
    }
}

void web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}