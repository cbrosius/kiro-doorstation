#ifndef API_SIP_H
#define API_SIP_H

#include "esp_http_server.h"

// Register SIP API handlers
esp_err_t api_sip_register(httpd_handle_t server);

#endif // API_SIP_H
