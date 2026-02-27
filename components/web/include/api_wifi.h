#ifndef API_WIFI_H
#define API_WIFI_H

#include "esp_http_server.h"

// Register WiFi API handlers
esp_err_t api_wifi_register(httpd_handle_t server);

#endif // API_WIFI_H
