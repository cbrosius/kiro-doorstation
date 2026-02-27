#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include "esp_http_server.h"

// Register System API handlers
esp_err_t api_system_register(httpd_handle_t server);

#endif // API_SYSTEM_H
