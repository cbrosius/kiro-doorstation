#ifndef API_AUTH_H
#define API_AUTH_H

#include "esp_http_server.h"

// Register Authentication API handlers
esp_err_t api_auth_register(httpd_handle_t server);

#endif // API_AUTH_H
