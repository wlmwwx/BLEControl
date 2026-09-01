/*
 * WiFi provisioning - STA with AP fallback.
 *
 * Boot behaviour:
 *   - saved SSID in NVS  -> STA mode, connect to that network
 *   - no saved SSID      -> AP mode (SSID "BTControl") for web provisioning
 *
 * STA -> AP fallback happens at runtime when the STA link cannot be
 * established (auth fail, repeated disconnects, or a connect timeout).
 * The switch is deferred to a small control task - esp_wifi_stop() must
 * never be called from inside a wifi event handler or timer callback.
 *
 * AP -> STA happens only via reboot after POST /wifi/config.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "mdns.h"

#include "wifi_prov.h"

static const char *TAG = "WIFI_PROV";

#define NVS_NAMESPACE    "wifi"
#define NVS_KEY_SSID     "ssid"
#define NVS_KEY_PASS     "pass"
#define NVS_KEY_NAME     "name"

#define AP_SSID          "BTControl"
#define AP_PASS          "12345678"
#define AP_MAX_CONN      3

#define STA_RETRY_MAX            3
#define STA_CONNECT_TIMEOUT_MS   30000

#define WIFI_CTRL_QUEUE_LEN      4

static const char *s_mode = "ap";          /* "ap" | "sta" */
static bool s_sta_connected = false;
static uint8_t s_sta_retries = 0;
static esp_timer_handle_t s_sta_timer = NULL;
static QueueHandle_t s_ctrl_queue = NULL;
static char s_sta_ssid[33] = {0};

typedef enum {
    WIFI_CTRL_SWITCH_TO_AP,
} wifi_ctrl_msg_t;

/* ------------------------------------------------------------------------ */
/* NVS helpers                                                              */
/* ------------------------------------------------------------------------ */

static bool load_wifi_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool found = (nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK);
    if (found) {
        size_t plen = pass_len;
        if (nvs_get_str(h, NVS_KEY_PASS, pass, &plen) != ESP_OK) {
            pass[0] = '\0';
        }
    }
    nvs_close(h);
    return found;
}

static void save_wifi_config(const char *ssid, const char *pass, const char *name, bool save)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    if (save) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_set_str(h, NVS_KEY_PASS, pass);
        if (name && name[0]) {
            nvs_set_str(h, NVS_KEY_NAME, name);
        }
    } else {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        /* the device name is kept across forget + re-provision */
    }
    nvs_commit(h); /* must commit before esp_restart() */
    nvs_close(h);
}

/* Effective device name: saved NVS value, or "BTControl-<last4-of-MAC>". */
void wifi_prov_get_device_name(char *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t l = len;
        if (nvs_get_str(h, NVS_KEY_NAME, buf, &l) == ESP_OK && l > 0) {
            nvs_close(h);
            return;
        }
        nvs_close(h);
    }
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
        snprintf(buf, len, "BTControl");
        return;
    }
    snprintf(buf, len, "BTControl-%02X%02X", mac[4], mac[5]);
}

/* mDNS hostname: lowercased, alphanumeric + '-', no leading/trailing '-'. */
void wifi_prov_get_mdns_hostname(char *buf, size_t len)
{
    char name[WIFI_PROV_NAME_MAX + 1];
    wifi_prov_get_device_name(name, sizeof(name));

    size_t n = 0;
    for (const char *p = name; *p && n + 1 < len; p++) {
        char c = (char)tolower((unsigned char)*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            buf[n++] = c;
        } else if (n > 0 && buf[n - 1] != '-') {
            buf[n++] = '-';   /* map '_', ' ', '.' to '-' */
        }
    }
    while (n > 0 && buf[n - 1] == '-') {
        n--;
    }
    buf[n] = '\0';
    if (n == 0) {
        snprintf(buf, len, "btcontrol");
    }
}

/* ------------------------------------------------------------------------ */
/* Mode setup                                                               */
/* ------------------------------------------------------------------------ */

/* Prominent boot/connect banner: shows how to reach the device on the
 * current network without digging into the router's DHCP client list. */
static void wifi_prov_print_banner(void)
{
    char ipstr[16] = "192.168.4.1";
    const char *ssid = "BTControl";
    if (strcmp(s_mode, "sta") == 0) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
                snprintf(ipstr, sizeof(ipstr), IPSTR, IP2STR(&ip.ip));
            }
        }
        ssid = s_sta_ssid;
    }
    char name[WIFI_PROV_NAME_MAX + 1];
    char host[WIFI_PROV_NAME_MAX + 1];
    wifi_prov_get_device_name(name, sizeof(name));
    wifi_prov_get_mdns_hostname(host, sizeof(host));
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " %s  [%s mode]", name, s_mode);
    ESP_LOGI(TAG, " SSID : %s", ssid);
    ESP_LOGI(TAG, " IP   : %s", ipstr);
    ESP_LOGI(TAG, " mDNS : %s.local", host);
    ESP_LOGI(TAG, " HTTP : http://%s", ipstr);
    ESP_LOGI(TAG, "============================================");
}

static void start_ap(void)
{
    if (s_sta_timer) {
        esp_timer_stop(s_sta_timer); /* no STA watchdog needed in AP mode */
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode = "ap";
    s_sta_connected = false;
    ESP_LOGI(TAG, "AP mode. SSID: %s, Password: %s", AP_SSID, AP_PASS);
    wifi_prov_print_banner();
}

static void start_sta(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold = { .authmode = WIFI_AUTH_WPA2_PSK },
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    if (pass[0] == '\0') {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_mode = "sta";
    s_sta_connected = false;
    s_sta_retries = 0;
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    ESP_LOGI(TAG, "STA mode. Connecting to SSID: %s", ssid);
}

/* Must run from wifi_ctrl_task, not from an event handler. */
static void switch_to_ap(void)
{
    if (strcmp(s_mode, "ap") == 0) {
        return;
    }
    ESP_LOGW(TAG, "Switching to AP mode");
    ESP_ERROR_CHECK(esp_wifi_stop());
    start_ap();
}

/* ------------------------------------------------------------------------ */
/* Event handlers                                                           */
/* ------------------------------------------------------------------------ */

static void request_switch_to_ap(void)
{
    if (!s_ctrl_queue) {
        return;
    }
    wifi_ctrl_msg_t msg = WIFI_CTRL_SWITCH_TO_AP;
    xQueueSend(s_ctrl_queue, &msg, 0);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA started, connecting...");
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "STA connected");
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "STA disconnected, reason=%d", d->reason);
        if (d->reason == WIFI_REASON_AUTH_FAIL) {
            /* wrong credentials - let the user re-provision */
            request_switch_to_ap();
        } else if (s_sta_retries++ < STA_RETRY_MAX) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "STA retry limit reached - falling back to AP");
            request_switch_to_ap();
        }
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "AP station connected");
        break;

    case WIFI_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "AP station disconnected");
        break;

    default:
        break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
    s_sta_connected = true;
    s_sta_retries = 0;
    if (s_sta_timer) {
        esp_timer_stop(s_sta_timer);
    }
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
    wifi_prov_print_banner();
}

static void sta_timeout_cb(void *arg)
{
    ESP_LOGW(TAG, "STA connect timeout - falling back to AP");
    request_switch_to_ap();
}

static void wifi_ctrl_task(void *arg)
{
    wifi_ctrl_msg_t msg;
    while (1) {
        if (xQueueReceive(s_ctrl_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg) {
            case WIFI_CTRL_SWITCH_TO_AP:
                switch_to_ap();
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

esp_err_t wifi_prov_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Both netifs must exist before esp_wifi_start(): the default netif
     * event handlers silently drop DHCP setup for any NULL netif. */
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               got_ip_handler, NULL));

    s_ctrl_queue = xQueueCreate(WIFI_CTRL_QUEUE_LEN, sizeof(wifi_ctrl_msg_t));
    xTaskCreate(wifi_ctrl_task, "wifi_ctrl", 4096, NULL, configMAX_PRIORITIES - 4, NULL);

    esp_timer_create_args_t targs = {
        .callback = sta_timeout_cb,
        .name = "sta_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_sta_timer));

    char ssid[33] = {0};
    char pass[65] = {0};
    if (load_wifi_config(ssid, sizeof(ssid), pass, sizeof(pass))) {
        start_sta(ssid, pass);
        esp_timer_start_once(s_sta_timer, (uint64_t)STA_CONNECT_TIMEOUT_MS * 1000);
    } else {
        start_ap();
    }

    /* mDNS - announces on both AP and STA (the mdns component hooks the
     * netif events itself, so no manual netif registration is needed). */
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed");
    } else {
        char name[WIFI_PROV_NAME_MAX + 1];
        char host[WIFI_PROV_NAME_MAX + 1];
        wifi_prov_get_device_name(name, sizeof(name));
        wifi_prov_get_mdns_hostname(host, sizeof(host));
        mdns_hostname_set(host);
        mdns_instance_name_set(name);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    return ESP_OK;
}

const char *wifi_prov_mode(void)
{
    return s_mode;
}

bool wifi_prov_is_connected(void)
{
    return s_sta_connected;
}

void wifi_prov_get_status_json(char *buf, size_t len)
{
    char ssid[33] = {0};
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t l = sizeof(ssid);
        nvs_get_str(h, NVS_KEY_SSID, ssid, &l);
        nvs_close(h);
    }

    const char *ifkey = (strcmp(s_mode, "sta") == 0) ? "WIFI_STA_DEF" : "WIFI_AP_DEF";
    char ipstr[16] = "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(ifkey);
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            snprintf(ipstr, sizeof(ipstr), IPSTR, IP2STR(&ip.ip));
        }
    }

    char name[WIFI_PROV_NAME_MAX + 1];
    wifi_prov_get_device_name(name, sizeof(name));

    snprintf(buf, len,
             "{\"mode\":\"%s\",\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"name\":\"%s\"}",
             s_mode, s_sta_connected ? "true" : "false", ssid, ipstr, name);
}

esp_err_t wifi_prov_set_config(const char *ssid, const char *pass, const char *name)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= 32 || strlen(pass) >= 64 ||
        (name && strlen(name) > WIFI_PROV_NAME_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }
    save_wifi_config(ssid, pass ? pass : "", name, true);
    return ESP_OK;
}

esp_err_t wifi_prov_forget(void)
{
    save_wifi_config("", "", NULL, false);
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* HTTP endpoints                                                           */
/* ------------------------------------------------------------------------ */

/* HTML-escape a string so it can be embedded in an attribute value. */
static void html_escape(const char *in, char *out, size_t out_sz)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 6 < out_sz; p++) {
        switch (*p) {
        case '&':  memcpy(out + n, "&amp;", 5);  n += 5; break;
        case '<':  memcpy(out + n, "&lt;", 4);   n += 4; break;
        case '>':  memcpy(out + n, "&gt;", 4);   n += 4; break;
        case '"':  memcpy(out + n, "&quot;", 6); n += 6; break;
        case '\'': memcpy(out + n, "&#39;", 5);  n += 5; break;
        default:   out[n++] = *p; break;
        }
    }
    out[n] = '\0';
}

static esp_err_t root_handler(httpd_req_t *req)
{
    /* Pre-fill the form with the currently saved config (SSID + device
     * name). The password is never echoed back. */
    char ssid[33] = {0};
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t l = sizeof(ssid);
        nvs_get_str(h, NVS_KEY_SSID, ssid, &l);
        nvs_close(h);
    }
    char name[WIFI_PROV_NAME_MAX + 1];
    wifi_prov_get_device_name(name, sizeof(name));

    char essid[64];
    char ename[128];
    html_escape(ssid, essid, sizeof(essid));
    html_escape(name, ename, sizeof(ename));

    char buf[1200];
    int len = snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>BTControl WiFi 配网</title></head>"
        "<body style=\"font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px\">"
        "<h2>BTControl WiFi 配网</h2>"
        "<p>输入 WiFi 名称和密码，设备保存后重启并连接该网络。</p>"
        "<p>设备名称可自定义（最多 18 字符），用于区分多个设备；留空则自动使用 BTControl-XXXX（MAC 后 4 位）。</p>"
        "<form method=\"POST\" action=\"/wifi/config\">"
        "<label>WiFi 名称 (SSID)</label><br>"
        "<input name=\"ssid\" required value=\"%s\" style=\"width:100%%;padding:8px;margin:8px 0\"><br>"
        "<label>密码 (Password)</label><br>"
        "<input type=\"password\" name=\"password\" style=\"width:100%%;padding:8px;margin:8px 0\"><br>"
        "<label>设备名称 (可选, 最多18字符)</label><br>"
        "<input name=\"name\" maxlength=\"18\" value=\"%s\" placeholder=\"BTControl-XXXX\" style=\"width:100%%;padding:8px;margin:8px 0\"><br>"
        "<button type=\"submit\" style=\"width:100%%;padding:10px\">保存并连接</button>"
        "</form></body></html>",
        essid, ename);
    if (len < 0 || len >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, buf, len);
}

static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    char buf[256];
    wifi_prov_get_status_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, strlen(buf));
}

/* Decode a form-urlencoded / JSON string field into out[].
 * Matches `key=value` (form) or `"key":"value"` (JSON), stops at `"` or `&`. */
static void extract_field(const char *buf, const char *key, char *out, size_t out_sz)
{
    out[0] = '\0';
    const char *p = strstr(buf, key);
    if (!p) {
        return;
    }
    p += strlen(key);
    while (*p && *p != '=' && *p != ':') {
        p++;
    }
    if (!*p) {
        return;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    if (*p == '"') {
        p++;
    }
    size_t n = 0;
    while (*p && *p != '"' && *p != '&' && n + 1 < out_sz) {
        if (*p == '+') {
            out[n++] = ' ';
            p++;
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], '\0' };
            out[n++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
}

static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return httpd_resp_send_500(req);
    }
    buf[ret] = '\0';

    char ssid[32] = {0};
    char pass[64] = {0};
    char name[WIFI_PROV_NAME_MAX + 1] = {0};
    extract_field(buf, "ssid", ssid, sizeof(ssid));
    extract_field(buf, "password", pass, sizeof(pass));
    extract_field(buf, "name", name, sizeof(name));
    if (ssid[0] == '\0') {
        ESP_LOGE(TAG, "Missing ssid in body: %s", buf);
        return httpd_resp_send_500(req);
    }

    if (wifi_prov_set_config(ssid, pass, name[0] ? name : NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save wifi config");
        return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "Saved wifi config for SSID '%s', rebooting...", ssid);

    httpd_resp_send(req, "{\"ok\":true}", -1);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK; /* unreachable */
}

static esp_err_t wifi_forget_handler(httpd_req_t *req)
{
    if (wifi_prov_forget() != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "WiFi config erased, rebooting into AP mode...");
    httpd_resp_send(req, "{\"ok\":true}", -1);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK; /* unreachable */
}

void wifi_prov_register_http(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = root_handler },
        { .uri = "/wifi/status", .method = HTTP_GET,  .handler = wifi_status_handler },
        { .uri = "/wifi/config", .method = HTTP_POST, .handler = wifi_config_handler },
        { .uri = "/wifi/forget", .method = HTTP_POST, .handler = wifi_forget_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
        }
    }
}
