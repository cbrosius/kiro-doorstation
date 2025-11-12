#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <stdbool.h>
#include "esp_http_server.h"

/**
 * @brief Start the captive portal HTTP server on port 80
 *
 * This starts a lightweight HTTP server that serves captive portal pages
 * and minimal WiFi configuration APIs. The server runs independently
 * from the main HTTPS web service.
 *
 * @return true if started successfully, false otherwise
 */
bool captive_portal_start(void);

/**
 * @brief Stop the captive portal HTTP server
 *
 * This stops the captive portal server and frees associated resources.
 */
void captive_portal_stop(void);

/**
 * @brief Register captive portal API handlers on a given HTTP server
 *
 * This registers the minimal set of WiFi-related API endpoints needed
 * for captive portal operation: /api/wifi/scan, /api/wifi/connect, /api/wifi/config.
 * These handlers are reused from the main web_api.c but registered without
 * authentication requirements.
 *
 * @param server HTTP server handle to register handlers on
 */
void captive_api_register_handlers(httpd_handle_t server);

/**
 * @brief Get the current status of WiFi credential testing
 *
 * This endpoint allows the captive portal to check if credential testing
 * is in progress and if a STA IP is available for redirect.
 *
 * @return JSON response with testing status and STA IP if available
 */
esp_err_t captive_get_status_handler(httpd_req_t *req);

#endif // CAPTIVE_PORTAL_H