#ifndef API_EMAIL_H
#define API_EMAIL_H

#include "esp_http_server.h"

// Register Email API handlers
esp_err_t api_email_register(httpd_handle_t server);

#endif // API_EMAIL_H
