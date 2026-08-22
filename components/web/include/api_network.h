#ifndef API_NETWORK_H
#define API_NETWORK_H

#include "esp_http_server.h"

// Register Network API handlers
esp_err_t api_network_register(httpd_handle_t server);

#endif // API_NETWORK_H
