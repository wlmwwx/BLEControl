/*
 * WiFi provisioning - STA with AP fallback
 */
#ifndef _WIFI_PROV_H_
#define _WIFI_PROV_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reads NVS and starts WiFi in STA (saved credentials) or AP (provisioning) mode.
 * Must be called after nvs_flash_init(). Creates both netifs and starts mDNS. */
esp_err_t wifi_prov_init(void);

/* Registers the provisioning endpoints: GET /, GET /wifi/status,
 * POST /wifi/config, POST /wifi/forget. Call after httpd_start(). */
void wifi_prov_register_http(httpd_handle_t server);

/* Current mode: "ap" or "sta" */
const char *wifi_prov_mode(void);

/* True once the STA link has an IP (always false in AP mode) */
bool wifi_prov_is_connected(void);

/* Writes {"mode":..,"connected":..,"ssid":..,"ip":..} into buf */
void wifi_prov_get_status_json(char *buf, size_t len);

/* Saves ssid/password to NVS (does NOT reboot - caller reboots) */
esp_err_t wifi_prov_set_config(const char *ssid, const char *pass);

/* Erases the saved WiFi config from NVS (does NOT reboot - caller reboots) */
esp_err_t wifi_prov_forget(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_PROV_H_ */
