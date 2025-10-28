#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include "esp_event.h"

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64
#define MAX_SCAN_RESULTS        20

// Callback for when AP mode is started
typedef void (*wifi_manager_ap_start_callback_t)(void);

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    int rssi;
    bool secure;
} wifi_scan_result_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASSWORD_MAX_LEN];
    bool configured;
} wifi_manager_config_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char ip_address[16];
    int rssi;
    bool connected;
} wifi_connection_info_t;

void wifi_manager_init(void);
bool wifi_is_connected(void);
void wifi_start_ap_mode(void);
void wifi_connect_sta(const char* ssid, const char* password);
void wifi_save_config(const char* ssid, const char* password);
wifi_manager_config_t wifi_load_config(void);
wifi_connection_info_t wifi_get_connection_info(void);
int wifi_scan_networks(wifi_scan_result_t** results);

// Parallel credential testing functions
bool wifi_test_credentials(const char* ssid, const char* password);
bool wifi_is_testing_credentials(void);
const char* wifi_get_tested_sta_ip(void);
void wifi_clear_tested_sta_ip(void);
void wifi_transition_to_sta_mode(void);

void wifi_manager_register_ap_start_callback(wifi_manager_ap_start_callback_t cb);

#endif