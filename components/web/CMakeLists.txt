set(web_srcs
    "api_auth.c"
    "api_cert.c"
    "api_dtmf.c"
    "api_email.c"
    "api_hardware.c"
    "api_network.c"
    "api_ntp.c"
    "api_ota.c"
    "api_sip.c"
    "api_system.c"
    "api_wifi.c"
    "web_api.c"
    "web_server.c"
    "web_utils.c"
)

idf_component_register(
    SRCS ${web_srcs}
    INCLUDE_DIRS "include"
    EMBED_FILES "index.html" "login.html" "setup.html" "documentation.html"
    REQUIRES esp_http_server json nvs_flash esp_wifi esp_netif esp_system esp_partition core
)
