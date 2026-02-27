#ifndef API_NTP_H
#define API_NTP_H

#include "esp_http_server.h"

// Register NTP API handlers
esp_err_t api_ntp_register(httpd_handle_t server);

#endif // API_NTP_H
