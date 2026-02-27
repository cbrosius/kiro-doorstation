#ifndef API_HARDWARE_H
#define API_HARDWARE_H

#include "esp_http_server.h"

// Register Hardware API handlers
esp_err_t api_hardware_register(httpd_handle_t server);

#endif // API_HARDWARE_H
