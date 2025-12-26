# Task 11: HTTP to HTTPS Redirect - Implementation Summary

## Overview
Implemented HTTP to HTTPS redirect functionality to automatically redirect all HTTP requests on port 80 to HTTPS on port 443 with a 301 Moved Permanently status.

## Changes Made

### 1. Added HTTP Redirect Server Handle
**File**: `main/web_server.c`

Added a new static variable to track the HTTP redirect server:
```c
static httpd_handle_t redirect_server = NULL;
```

### 2. Implemented HTTP Redirect Handler
**File**: `main/web_server.c`

Created a redirect handler that works as a 404 error handler:
- Extracts the Host header from the incoming HTTP request
- Constructs the HTTPS URL using the host and original URI
- Sends a 301 Moved Permanently response with Location header
- Logs the redirect for debugging

```c
static esp_err_t http_redirect_handler(httpd_req_t *req, httpd_err_code_t err __attribute__((unused)))
{
    // Get the Host header to construct the HTTPS URL
    char host[128] = {0};
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    
    if (host_len > 0 && host_len < sizeof(host)) {
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    } else {
        // Fallback to IP address if Host header is missing
        strcpy(host, "192.168.4.1");  // Default AP IP
    }
    
    // Construct HTTPS URL with sufficient buffer size
    // Max: "https://" (8) + host (127) + uri (512) + null (1) = 648 bytes
    char https_url[700];
    snprintf(https_url, sizeof(https_url), "https://%s%s", host, req->uri);
    
    // Send 301 Moved Permanently redirect
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", https_url);
    httpd_resp_send(req, NULL, 0);
    
    ESP_LOGI(TAG, "HTTP redirect: %s -> %s", req->uri, https_url);
    
    return ESP_OK;
}
```

### 3. Registered as Error Handler
**File**: `main/web_server.c`

Instead of using a wildcard URI handler (which isn't supported), the redirect handler is registered as a 404 error handler to catch all unmatched requests:
```c
httpd_register_err_handler(redirect_server, HTTPD_404_NOT_FOUND, http_redirect_handler);
```

### 4. Updated web_server_start()
**File**: `main/web_server.c`

Added code to start the HTTP redirect server on port 80 after the HTTPS server starts:
```c
// Start HTTP redirect server on port 80
httpd_config_t redirect_config = HTTPD_DEFAULT_CONFIG();
redirect_config.server_port = 80;
redirect_config.ctrl_port = 32769;  // Different control port
redirect_config.max_uri_handlers = 0;  // No URI handlers needed

if (httpd_start(&redirect_server, &redirect_config) == ESP_OK) {
    // Register error handler for 404 to redirect all requests to HTTPS
    httpd_register_err_handler(redirect_server, HTTPD_404_NOT_FOUND, http_redirect_handler);
    ESP_LOGI(TAG, "HTTP redirect server started on port 80");
} else {
    ESP_LOGW(TAG, "Failed to start HTTP redirect server on port 80");
}
```

### 5. Updated web_server_stop()
**File**: `main/web_server.c`

Added code to stop the HTTP redirect server when the web server is stopped:
```c
if (redirect_server) {
    httpd_stop(redirect_server);
    redirect_server = NULL;
    ESP_LOGI(TAG, "HTTP redirect server stopped");
}
```

## Requirements Satisfied

✅ **4.5**: WHEN HTTP request is received on port 80, THE System SHALL redirect to HTTPS port 443

## Implementation Details

### Port Configuration
- **HTTP Server**: Port 80 (redirect only)
- **HTTPS Server**: Port 443 (main application)
- **Control Ports**: 32768 (HTTPS), 32769 (HTTP redirect)

### Redirect Behavior
1. Client makes HTTP request to `http://device-ip/path`
2. HTTP server receives request on port 80
3. Since no URI handlers are registered, all requests trigger a 404 error
4. The 404 error handler (redirect handler) is invoked
5. Server extracts Host header (or uses default IP)
6. Server constructs HTTPS URL: `https://device-ip/path`
7. Server sends 301 Moved Permanently with Location header
8. Client automatically follows redirect to HTTPS
9. Connection is closed after redirect

### Technical Approach
The implementation uses the HTTP server's error handling mechanism rather than URI handlers. Since ESP-IDF's HTTP server doesn't support wildcard URI patterns like `/*`, we register the redirect handler as a 404 error handler. This means:
- No URI handlers are registered on the HTTP server
- All incoming requests result in 404 errors
- The custom 404 handler intercepts these and performs the redirect
- This approach works for all HTTP methods (GET, POST, etc.)

### Fallback Handling
If the Host header is missing (rare case), the handler falls back to the default AP IP address (192.168.4.1).

### Logging
All redirects are logged with the format:
```
HTTP redirect: /original/path -> https://host/original/path
```

## Testing Recommendations

1. **Basic Redirect Test**
   - Access `http://device-ip/` in browser
   - Verify automatic redirect to `https://device-ip/`

2. **Path Preservation Test**
   - Access `http://device-ip/api/system/status`
   - Verify redirect to `https://device-ip/api/system/status`

3. **Host Header Test**
   - Access using hostname: `http://doorstation.local/`
   - Verify redirect preserves hostname: `https://doorstation.local/`

4. **Server Lifecycle Test**
   - Start web server
   - Verify both HTTP (80) and HTTPS (443) servers are running
   - Stop web server
   - Verify both servers are stopped

## Notes

- The HTTP redirect server uses a 404 error handler approach
- Uses a separate control port (32769) to avoid conflicts with HTTPS server
- Gracefully handles failure to start HTTP server (logs warning but doesn't fail HTTPS)
- Properly cleans up resources when stopping servers
- Follows HTTP 301 standard for permanent redirects

## Troubleshooting

If the HTTP redirect server fails to start:
1. **Socket limit**: ESP32 has a limited number of sockets. Check `CONFIG_LWIP_MAX_SOCKETS` in sdkconfig
2. **Port conflict**: Ensure port 80 is not already in use by another service
3. **Memory**: The HTTP server requires heap memory. Check available heap with `esp_get_free_heap_size()`
4. **Configuration**: Verify `CONFIG_HTTPD_MAX_REQ_HDR_LEN` and other HTTP server configs in sdkconfig

The HTTPS server on port 443 will continue to work even if the HTTP redirect server fails to start. Users can still access the device directly via HTTPS.
