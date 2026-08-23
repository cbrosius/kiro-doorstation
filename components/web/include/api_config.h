#ifndef API_CONFIG_H
#define API_CONFIG_H

#include "esp_http_server.h"

// Register Config API handlers
esp_err_t api_config_register(httpd_handle_t server);

#endif // API_CONFIG_H
