#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Start the PiLab Panel WiFi service. The panel always brings up the built-in
// recovery/configuration access point. If saved STA credentials exist in NVS,
// it also connects to that infrastructure network using AP+STA mode.
esp_err_t panel_wifi_start(void);

// Backward-compatible name used by older app_main versions.
esp_err_t panel_wifi_start_ap(void);

// Save STA credentials to NVS and connect immediately. The AP remains active so
// the operator is not locked out while the station connection is being tested.
// If hostname is non-empty, it is sanitized, saved to NVS, and advertised by mDNS.
esp_err_t panel_wifi_connect_and_save(const char *ssid, const char *password, const char *hostname);

// Save/update the device hostname used for mDNS, e.g. http://pilab-panel.local.
// The value is sanitized to lowercase letters, numbers, and dashes.
esp_err_t panel_wifi_set_hostname(const char *hostname);

// Remove saved STA credentials, disconnect the station, and leave only the
// built-in PiLab-Panel access point active.
esp_err_t panel_wifi_forget_sta(void);


// True when station mode has completed DHCP and has a usable IP address.
// MQTT uses this to avoid DNS/connect attempts while the panel is still only
// running on its recovery AP.
bool panel_wifi_sta_is_connected(void);

// Allocate JSON response strings. Caller must free().
esp_err_t panel_wifi_status_json(char **out_json);
esp_err_t panel_wifi_scan_json(char **out_json);

#ifdef __cplusplus
}
#endif
