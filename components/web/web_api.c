#include "web_api.h"
#include "esp_http_server.h"
#include "esp_log.h"


// Include extracted API modules
#include "api_auth.h"
#include "api_cert.h"
#include "api_dtmf.h"
#include "api_email.h"
#include "api_hardware.h"
#include "api_network.h"
#include "api_ntp.h"
#include "api_ota.h"
#include "api_sip.h"
#include "api_system.h"
#include "api_wifi.h"


static const char *TAG = "WEB_API";

esp_err_t web_api_register_handlers(httpd_handle_t server) {
  ESP_LOGI(TAG, "Registering all web API handlers");

  if (server == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Register all modules
  api_sip_register(server);
  api_wifi_register(server);
  api_network_register(server);
  api_email_register(server);
  api_ota_register(server);
  api_system_register(server);
  api_ntp_register(server);
  api_dtmf_register(server);
  api_hardware_register(server);
  api_cert_register(server);
  api_auth_register(server);

  ESP_LOGI(TAG, "Web API handler registration completed");
  return ESP_OK;
}
