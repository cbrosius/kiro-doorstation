#ifndef API_CERT_H
#define API_CERT_H

#include "esp_http_server.h"

// Register Certificate API handlers
esp_err_t api_cert_register(httpd_handle_t server);

#endif // API_CERT_H
