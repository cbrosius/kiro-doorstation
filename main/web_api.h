#ifndef WEB_API_H
#define WEB_API_H

#include "esp_http_server.h"

// Register all API endpoint handlers with the server
void web_api_register_handlers(httpd_handle_t server);

#endif
