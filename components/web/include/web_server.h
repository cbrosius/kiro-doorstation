#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"

// Server lifecycle functions
void web_server_start(void);
void web_server_stop(void);

// Authentication filter (exposed for API module)
esp_err_t auth_filter(httpd_req_t *req, bool extend_session);

#endif