#ifndef WEB_UTILS_H
#define WEB_UTILS_H

#include "cJSON.h"
#include "esp_http_server.h"


/**
 * @brief Send a JSON success response
 *
 * @param req The HTTP request handle
 * @param message The success message
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_response_json_success(httpd_req_t *req, const char *message);

/**
 * @brief Send a JSON error response
 *
 * @param req The HTTP request handle
 * @param code The HTTP error code (e.g. 400, 500)
 * @param message The error message
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_response_json_error(httpd_req_t *req, httpd_err_code_t code,
                                   const char *message);

/**
 * @brief Send a cJSON object as response and clean it up
 *
 * @param req The HTTP request handle
 * @param root The cJSON object to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_response_json_data(httpd_req_t *req, cJSON *root);

/**
 * @brief Parse JSON body from request
 *
 * @param req The HTTP request handle
 * @return cJSON* The parsed cJSON object, or NULL if failed
 */
cJSON *http_parse_json_body(httpd_req_t *req);

#endif // WEB_UTILS_H
