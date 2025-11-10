idf_component_register(
    SRCS
        "main.c"
        "sip_client.c"
        "audio_handler.c"
        "dtmf_decoder.c"
        "gpio_handler.c"
        "wifi_manager.c"
        "web_server.c"
        "web_api.c"
        "ntp_sync.c"
        "rtp_handler.c"
        "hardware_test.c"
        "auth_manager.c"
        "cert_manager.c"
        "ota_handler.c"
        "captive_portal.c"
        "dns_responder.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_http_server
        esp_https_server
        esp_netif
        nvs_flash
        driver
        esp_timer
        esp_adc
        json
        mbedtls
        app_update
)

target_add_binary_data(${COMPONENT_TARGET} "index.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "documentation.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "login.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "setup.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "captive_setup.html" TEXT)