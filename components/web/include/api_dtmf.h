#ifndef API_DTMF_H
#define API_DTMF_H

#include "esp_http_server.h"

// Register DTMF API handlers
esp_err_t api_dtmf_register(httpd_handle_t server);

#endif // API_DTMF_H
