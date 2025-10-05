#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include "esp_event.h"

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASSWORD_MAX_LEN   64
#define MAX_SCAN_RESULTS        20

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    int rssi;
    bool locked;
} wifi_scan_result_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASSWORD_MAX_LEN];
    bool configured;
} wifi_manager_config_t;

void wifi_manager_init(void);
bool wifi_is_connected(void);
void wifi_start_ap_mode(void);
void wifi_connect_sta(const char* ssid, const char* password);
void wifi_save_config(const char* ssid, const char* password);
wifi_manager_config_t wifi_load_config(void);
void wifi_clear_config(void);
char* wifi_scan_networks(void);

// ESPHome-style scan result management
void wifi_start_background_scan(void);
int wifi_get_scan_results(wifi_scan_result_t* results, int max_results);
void wifi_clear_scan_results(void);

#endif