/*
 * Phase 1: ESP32-C3 BLE Keyboard + WiFi HTTP API
 *
 * 支持: Keyboard, Mouse, Consumer Control
 * WiFi + HTTP API 服务器
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "esp_bt.h"
#if CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#endif
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_http_server.h"
#include "esp_hid_gap.h"

static const char *TAG = "BLE_HID_POC";

#define HID_BATTERY_LEVEL 100
#define QUEUE_SIZE 32

// Connection state
static bool s_ble_connected = false;
static esp_hidd_dev_t *s_hid_dev = NULL;

// Command queue
typedef struct {
    char cmd[32];
    char data[256];
} cmd_item_t;

static cmd_item_t s_cmd_queue[QUEUE_SIZE];
static int s_queue_head = 0;
static int s_queue_tail = 0;
static int s_queue_count = 0;

static void queue_push(const char *cmd, const char *data)
{
    if (s_queue_count >= QUEUE_SIZE) return;
    cmd_item_t *item = &s_cmd_queue[s_queue_tail];
    strncpy(item->cmd, cmd, sizeof(item->cmd) - 1);
    strncpy(item->data, data, sizeof(item->data) - 1);
    s_cmd_queue[s_queue_tail].cmd[sizeof(item->cmd) - 1] = 0;
    s_cmd_queue[s_queue_tail].data[sizeof(item->data) - 1] = 0;
    s_queue_tail = (s_queue_tail + 1) % QUEUE_SIZE;
    s_queue_count++;
}

static bool queue_pop(char *cmd, char *data, int len)
{
    if (s_queue_count == 0) return false;
    cmd_item_t *item = &s_cmd_queue[s_queue_head];
    strncpy(cmd, item->cmd, len - 1);
    strncpy(data, item->data, len - 1);
    cmd[len - 1] = 0;
    data[len - 1] = 0;
    s_queue_head = (s_queue_head + 1) % QUEUE_SIZE;
    s_queue_count--;
    return true;
}

static void process_command(const char *cmd, const char *data)
{
    ESP_LOGI(TAG, "CMD: %s -> %s", cmd, data);
    // Parse and execute command
    if (strcmp(cmd, "keyboard_type") == 0) {
        // Type text - already handled in HTTP handler
    } else if (strcmp(cmd, "keyboard_key") == 0) {
        // Key combination - already handled in HTTP handler
    } else if (strcmp(cmd, "mouse_move") == 0) {
        // Move mouse - already handled in HTTP handler
    } else if (strcmp(cmd, "mouse_click") == 0) {
        // Click - already handled in HTTP handler
    } else if (strcmp(cmd, "wait") == 0) {
        int ms = atoi(data);
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
}

static void queue_task(void *pvParameters)
{
    char cmd[64], data[256];
    while (1) {
        while (queue_pop(cmd, data, sizeof(cmd))) {
            process_command(cmd, data);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// USB HID Key codes (from USB HID spec)
#define USB_HID_MODIFIER_LEFT_CTRL   0x01
#define USB_HID_MODIFIER_LEFT_SHIFT  0x02
#define USB_HID_MODIFIER_LEFT_ALT    0x04
#define USB_HID_MODIFIER_LEFT_GUI    0x08

// HID Report Descriptors
// Report ID 1: Keyboard
const unsigned char keyboardReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0 - Left Ctrl)
    0x29, 0xE7,        //   Usage Maximum (0xE7 - Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8) - 8 modifier keys
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x03,        //   Output (Const,Var,Abs)
    0x95, 0x05,        //   Report Count (5) - 5 key codes
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0,              // End Collection
};

// Report ID 2: Mouse
const unsigned char mouseReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5) - padding
    0x81, 0x03,        //     Input (Const,Var,Abs)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data,Var,Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// Report ID 3: Consumer Control
const unsigned char consumerReportMap[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16) - one bit per usage = 2 bytes
    0x09, 0xB0,        //   Usage (Play)                  bit 0
    0x09, 0xB1,        //   Usage (Pause)                 bit 1
    0x09, 0xB2,        //   Usage (Record)                bit 2
    0x09, 0xB3,        //   Usage (Fast Forward)          bit 3
    0x09, 0xB4,        //   Usage (Rewind)                bit 4
    0x09, 0xB5,        //   Usage (Scan Next Track)       bit 5
    0x09, 0xB6,        //   Usage (Scan Previous Track)   bit 6
    0x09, 0xB7,        //   Usage (Stop)                  bit 7
    0x09, 0xB8,        //   Usage (Eject)                 bit 8
    0x09, 0xB9,        //   Usage (Random Play)           bit 9
    0x09, 0xCD,        //   Usage (Play/Pause)            bit 10
    0x09, 0xE2,        //   Usage (Mute)                  bit 11
    0x09, 0xE9,        //   Usage (Volume Increment)      bit 12
    0x09, 0xEA,        //   Usage (Volume Decrement)      bit 13
    0x09, 0x30,        //   Usage (Power)                 bit 14
    0x09, 0x40,        //   Usage (Menu)                  bit 15
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0xC0,              // End Collection
};

// HID Report Maps
static esp_hid_raw_report_map_t ble_report_maps[] = {
    { .data = keyboardReportMap, .len = sizeof(keyboardReportMap) },
    { .data = mouseReportMap, .len = sizeof(mouseReportMap) },
    { .data = consumerReportMap, .len = sizeof(consumerReportMap) },
};

// HID Device Configuration
static esp_hid_device_config_t ble_hid_config = {
    .vendor_id      = 0x16C0,
    .product_id     = 0x05DF,
    .version        = 0x0100,
    .device_name    = "BTControl-POC",
    .manufacturer_name = "ESP32-C3",
    .serial_number  = "1234567890",
    .report_maps    = ble_report_maps,
    .report_maps_len = sizeof(ble_report_maps) / sizeof(esp_hid_raw_report_map_t),
};

// HID Event Callback
static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "START - Starting BLE advertising...");
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "CONNECT");
        s_ble_connected = true;
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "DISCONNECT");
        s_ble_connected = false;
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(TAG, "OUTPUT[ID:%d, Len:%d]", param->output.report_id, param->output.length);
        break;
    default:
        break;
    }
}

// Send Keyboard Report
void send_keyboard_report(uint8_t modifiers, uint8_t key1, uint8_t key2, uint8_t key3)
{
    if (!s_ble_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "BLE not connected");
        return;
    }
    uint8_t buffer[8] = { modifiers, 0, key1, key2, key3, 0, 0, 0 };
    esp_hidd_dev_input_set(s_hid_dev, 0, 1, buffer, 8);
}

void send_keyboard_release(void)
{
    if (!s_ble_connected || s_hid_dev == NULL) return;
    uint8_t buffer[8] = { 0 };
    esp_hidd_dev_input_set(s_hid_dev, 0, 1, buffer, 8);
}

// Send Mouse Report
void send_mouse_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel)
{
    if (!s_ble_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "BLE not connected");
        return;
    }
    uint8_t buffer[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    esp_hidd_dev_input_set(s_hid_dev, 0, 2, buffer, 4);
}

// Consumer page usage -> bit position, matching the order declared in
// consumerReportMap. The report is a bitmap, not an array of usage codes.
typedef struct {
    uint8_t usage;
    uint8_t bit;
} consumer_key_t;

static const consumer_key_t consumer_key_map[] = {
    { 0xB0, 0 },  { 0xB1, 1 },  { 0xB2, 2 },  { 0xB3, 3 },
    { 0xB4, 4 },  { 0xB5, 5 },  { 0xB6, 6 },  { 0xB7, 7 },
    { 0xB8, 8 },  { 0xB9, 9 },  { 0xCD, 10 }, { 0xE2, 11 },
    { 0xE9, 12 }, { 0xEA, 13 }, { 0x30, 14 }, { 0x40, 15 },
};

static uint16_t consumer_usage_to_bitmap(uint8_t usage)
{
    if (usage == 0) {
        return 0;    // no key pressed - also used to release
    }
    for (size_t i = 0; i < sizeof(consumer_key_map) / sizeof(consumer_key_map[0]); i++) {
        if (consumer_key_map[i].usage == usage) {
            return (uint16_t)1 << consumer_key_map[i].bit;
        }
    }
    ESP_LOGW(TAG, "Unsupported consumer usage: 0x%02X", usage);
    return 0;
}

// Send Consumer Control Report
void send_consumer_report(uint8_t usage)
{
    if (!s_ble_connected || s_hid_dev == NULL) {
        ESP_LOGW(TAG, "BLE not connected");
        return;
    }
    uint16_t bitmap = consumer_usage_to_bitmap(usage);
    uint8_t buffer[2] = { (uint8_t)(bitmap & 0xFF), (uint8_t)(bitmap >> 8) };
    esp_hidd_dev_input_set(s_hid_dev, 0, 3, buffer, 2);
}

// USB HID Keycode lookup for ASCII characters
static const uint8_t usb_keycode_map[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2A, 0, 0, 0, 0, 0,     // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         // 0x10-0x1F
    0x2C, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,         // 0x20-0x27: space, !
    0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2F, 0x30, 0x31,         // 0x28-0x2F: " # $ % & '
    0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,         // 0x30-0x37: ( ) * + , -
    0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x5B,         // 0x38-0x3F: / 0-7 =
    0x5C, 0x5D, 0x2E, 0x1C, 0x1D, 0x1A, 0x1B, 0x22,         // 0x40-0x47: @ A-D
    0x23, 0x24, 0x25, 0x26, 0x33, 0x34, 0x35, 0x36,         // 0x48-0x4F: E-L
    0x37, 0x38, 0x27, 0x0E, 0x0F, 0x13, 0x10, 0x11,         // 0x50-0x57: M-T
    0x12, 0x2D, 0x31, 0x2E, 0x11, 0x00, 0x00, 0x00,         /* 0x58-0x5F: U-Z [ */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,         // 0x60-0x67: `
    0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,         // 0x68-0x6F: a-h
    0x26, 0x27, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,         // 0x70-0x77: i-p
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x00,         // 0x78-0x7F: q-x
};

static uint8_t ascii_to_keycode(unsigned char c)
{
    if (c > 127) return 0;
    return usb_keycode_map[c];
}

// WiFi configuration
#define WIFI_SSID     "BTControl"
#define WIFI_PASS     "12345678"
#define MAX_CONNECTIONS 3

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "WiFi station connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "WiFi station disconnected");
    }
}

// Initialize WiFi AP
static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", WIFI_SSID, WIFI_PASS);
}

// HTTP GET /status - returns connection status
static esp_err_t status_handler(httpd_req_t *req)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"connected\":%s,\"device_name\":\"%s\"}",
             s_ble_connected ? "true" : "false",
             ble_hid_config.device_name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

// HTTP POST /keyboard/type - type a string
static esp_err_t keyboard_type_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);

    buf[ret] = '\0';
    ESP_LOGI(TAG, "keyboard/type: %s", buf);

    // Parse JSON and extract "text" field
    char *text_start = strstr(buf, "\"text\"");
    if (text_start) {
        text_start = strchr(text_start, ':');
        if (text_start) {
            text_start++;
            while (*text_start && !isprint(*text_start)) text_start++;
            if (*text_start == '"') text_start++;
            char *text_end = text_start;
            while (*text_end && *text_end != '"' && isprint(*text_end)) text_end++;
            *text_end = '\0';
            ESP_LOGI(TAG, "Typing: '%s'", text_start);

            // Send each character
            while (*text_start) {
                char c = *text_start++;
                uint8_t modifiers = 0;
                uint8_t keycode = ascii_to_keycode(c);
                if (c >= 'A' && c <= 'Z') modifiers = USB_HID_MODIFIER_LEFT_SHIFT;
                else if (c >= 'a' && c <= 'z') keycode = ascii_to_keycode(c - 'a' + 'A');
                if (keycode) {
                    send_keyboard_report(modifiers, keycode, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    send_keyboard_release();
                    vTaskDelay(30 / portTICK_PERIOD_MS);
                }
            }
            httpd_resp_send(req, "{\"ok\":true}", -1);
            return ESP_OK;
        }
    }
    return httpd_resp_send_500(req);
}

// HTTP POST /keyboard/key - send a key combination
static esp_err_t keyboard_key_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);

    buf[ret] = '\0';
    ESP_LOGI(TAG, "keyboard/key: %s", buf);

    // Expected format: {"keys":["CTRL","C"]}
    char *keys_start = strstr(buf, "\"keys\"");
    if (keys_start) {
        uint8_t modifiers = 0;
        uint8_t keycode = 0;

        // Simple parsing for modifier keys
        if (strstr(buf, "\"CTRL\"")) modifiers |= USB_HID_MODIFIER_LEFT_CTRL;
        if (strstr(buf, "\"SHIFT\"")) modifiers |= USB_HID_MODIFIER_LEFT_SHIFT;
        if (strstr(buf, "\"ALT\"")) modifiers |= USB_HID_MODIFIER_LEFT_ALT;
        if (strstr(buf, "\"GUI\"")) modifiers |= USB_HID_MODIFIER_LEFT_GUI;

        // Find the key code
        char *keycodes[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M",
                           "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
                           "ENTER","ESCAPE","BACKSPACE","TAB","SPACE","UP","DOWN","LEFT","RIGHT"};
        uint8_t codes[] = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                          0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                          0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
                          0x52, 0x51, 0x50, 0x4F};
        for (int i = 0; i < sizeof(keycodes)/sizeof(keycodes[0]); i++) {
            char search[8];
            snprintf(search, sizeof(search), "\"%s\"", keycodes[i]);
            if (strstr(buf, search)) {
                keycode = codes[i];
                break;
            }
        }

        if (keycode) {
            send_keyboard_report(modifiers, keycode, 0, 0);
            vTaskDelay(50 / portTICK_PERIOD_MS);
            send_keyboard_release();
            httpd_resp_send(req, "{\"ok\":true}", -1);
            return ESP_OK;
        }
    }
    return httpd_resp_send_500(req);
}

// HTTP POST /mouse/move - move mouse
static esp_err_t mouse_move_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int dx = 0, dy = 0;
    sscanf(buf, "{\"dx\":%d,\"dy\":%d}", &dx, &dy);
    send_mouse_report(0, dx, dy, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/click - mouse click
static esp_err_t mouse_click_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int button = 1; // default left click
    char *btn_str = strstr(buf, "\"button\"");
    if (btn_str) sscanf(btn_str, "\"button\":%d", &button);

    send_mouse_report(button, 0, 0, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    send_mouse_report(0, 0, 0, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/scroll - mouse scroll
static esp_err_t mouse_scroll_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int wheel = 0;
    sscanf(buf, "{\"scroll\":%d}", &wheel);
    send_mouse_report(0, 0, 0, (int8_t)wheel);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/drag - mouse drag (move + button down)
static esp_err_t mouse_drag_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int dx = 0, dy = 0;
    sscanf(buf, "{\"dx\":%d,\"dy\":%d}", &dx, &dy);

    // Mouse down (left button)
    send_mouse_report(1, 0, 0, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Move while dragging
    send_mouse_report(1, dx, dy, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Mouse up
    send_mouse_report(0, 0, 0, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/double_click - double click
static esp_err_t mouse_doubleclick_handler(httpd_req_t *req)
{
    // Left click twice quickly
    send_mouse_report(1, 0, 0, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    send_mouse_report(0, 0, 0, 0);
    vTaskDelay(30 / portTICK_PERIOD_MS);
    send_mouse_report(1, 0, 0, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    send_mouse_report(0, 0, 0, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/press - press and hold mouse button
static esp_err_t mouse_press_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int button = 1;
    char *btn_str = strstr(buf, "\"button\"");
    if (btn_str) sscanf(btn_str, "\"button\":%d", &button);

    send_mouse_report(button, 0, 0, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /mouse/release - release mouse button
static esp_err_t mouse_release_handler(httpd_req_t *req)
{
    send_mouse_report(0, 0, 0, 0);
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP POST /consumer - consumer control (media keys)
static esp_err_t consumer_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    int usage = 0;
    sscanf(buf, "{\"usage\":%d}", &usage);
    if (usage) {
        send_consumer_report(usage);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        send_consumer_report(0);
    }
    httpd_resp_send(req, "{\"ok\":true}", -1);
    return ESP_OK;
}

// HTTP GET /info - device info
static esp_err_t info_handler(httpd_req_t *req)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"device\":\"%s\",\"free_heap\":%lu}",
             ble_hid_config.device_name,
             (unsigned long)esp_get_free_heap_size());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

// HTTP POST /queue/add - add command to queue
static esp_err_t queue_add_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return httpd_resp_send_500(req);
    buf[ret] = '\0';

    char cmd[32] = {0}, data[256] = {0};
    sscanf(buf, "{\"cmd\":\"%31[^\"]\",\"data\":\"%255[^\"]\"}", cmd, data);
    queue_push(cmd, data);
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"queue_len\":%d}", s_queue_count);
    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

// HTTP POST /queue/exec - execute queued commands
static esp_err_t queue_exec_handler(httpd_req_t *req)
{
    char cmd[64], data[256];
    int count = 0;
    while (queue_pop(cmd, data, sizeof(cmd))) {
        process_command(cmd, data);
        count++;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"executed\":%d}", count);
    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

// Start HTTP server
static void http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;   // 14 endpoints are registered below; default is 8

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    // Register handlers
    httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
    httpd_register_uri_handler(server, &status_uri);

    httpd_uri_t type_uri = { .uri = "/keyboard/type", .method = HTTP_POST, .handler = keyboard_type_handler };
    httpd_register_uri_handler(server, &type_uri);

    httpd_uri_t key_uri = { .uri = "/keyboard/key", .method = HTTP_POST, .handler = keyboard_key_handler };
    httpd_register_uri_handler(server, &key_uri);

    httpd_uri_t move_uri = { .uri = "/mouse/move", .method = HTTP_POST, .handler = mouse_move_handler };
    httpd_register_uri_handler(server, &move_uri);

    httpd_uri_t click_uri = { .uri = "/mouse/click", .method = HTTP_POST, .handler = mouse_click_handler };
    httpd_register_uri_handler(server, &click_uri);

    httpd_uri_t scroll_uri = { .uri = "/mouse/scroll", .method = HTTP_POST, .handler = mouse_scroll_handler };
    httpd_register_uri_handler(server, &scroll_uri);

    httpd_uri_t drag_uri = { .uri = "/mouse/drag", .method = HTTP_POST, .handler = mouse_drag_handler };
    httpd_register_uri_handler(server, &drag_uri);

    httpd_uri_t dblclick_uri = { .uri = "/mouse/double_click", .method = HTTP_POST, .handler = mouse_doubleclick_handler };
    httpd_register_uri_handler(server, &dblclick_uri);

    httpd_uri_t press_uri = { .uri = "/mouse/press", .method = HTTP_POST, .handler = mouse_press_handler };
    httpd_register_uri_handler(server, &press_uri);

    httpd_uri_t release_uri = { .uri = "/mouse/release", .method = HTTP_POST, .handler = mouse_release_handler };
    httpd_register_uri_handler(server, &release_uri);

    // Queue endpoint for batch commands
    httpd_uri_t queue_uri = { .uri = "/queue/add", .method = HTTP_POST, .handler = queue_add_handler };
    httpd_register_uri_handler(server, &queue_uri);

    httpd_uri_t queue_exec_uri = { .uri = "/queue/exec", .method = HTTP_POST, .handler = queue_exec_handler };
    httpd_register_uri_handler(server, &queue_exec_uri);

    httpd_uri_t consumer_uri = { .uri = "/consumer", .method = HTTP_POST, .handler = consumer_handler };
    httpd_register_uri_handler(server, &consumer_uri);

    httpd_uri_t info_uri = { .uri = "/info", .method = HTTP_GET, .handler = info_handler };
    httpd_register_uri_handler(server, &info_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}

// Interactive test task via UART
void interactive_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "BLE HID POC - Interactive Test Mode");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Commands:");
    ESP_LOGI(TAG, "  k <char>     - Send keyboard key");
    ESP_LOGI(TAG, "  ks <char>    - Send keyboard key with Shift");
    ESP_LOGI(TAG, "  kc <text>    - Send keyboard text string");
    ESP_LOGI(TAG, "  m <dx> <dy>  - Send mouse move");
    ESP_LOGI(TAG, "  mc <btn>     - Send mouse click (1=left, 2=right, 4=middle)");
    ESP_LOGI(TAG, "  mu          - Send mouse up (release all buttons)");
    ESP_LOGI(TAG, "  sc <key>     - Send consumer control:");
    ESP_LOGI(TAG, "                  v=Volume Up, V=Volume Down, m=Mute");
    ESP_LOGI(TAG, "                  p=Play, P=Pause, s=Stop");
    ESP_LOGI(TAG, "                  n=Next, b=Previous");
    ESP_LOGI(TAG, "  status       - Show connection status");
    ESP_LOGI(TAG, "========================================");

    char line[128];
    while (1) {
        printf("\n> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        line[strcspn(line, "\n")] = 0;

        if (strncmp(line, "k ", 2) == 0) {
            char c = line[2];
            uint8_t keycode = ascii_to_keycode(c);
            if (keycode) {
                send_keyboard_report(0, keycode, 0, 0);
                vTaskDelay(50 / portTICK_PERIOD_MS);
                send_keyboard_release();
                ESP_LOGI(TAG, "Sent key: '%c' (0x%02x)", c, keycode);
            }
        }
        else if (strncmp(line, "ks ", 3) == 0) {
            char c = line[3];
            uint8_t keycode = ascii_to_keycode(c);
            if (keycode) {
                send_keyboard_report(USB_HID_MODIFIER_LEFT_SHIFT, keycode, 0, 0);
                vTaskDelay(50 / portTICK_PERIOD_MS);
                send_keyboard_release();
                ESP_LOGI(TAG, "Sent Shift+%c", c);
            }
        }
        else if (strncmp(line, "kc ", 3) == 0) {
            char *text = line + 3;
            ESP_LOGI(TAG, "Sending text: '%s'", text);
            while (*text) {
                char c = *text++;
                uint8_t modifiers = 0;
                uint8_t keycode = ascii_to_keycode(c);
                if (c >= 'A' && c <= 'Z') {
                    modifiers = USB_HID_MODIFIER_LEFT_SHIFT;
                }
                if (keycode) {
                    send_keyboard_report(modifiers, keycode, 0, 0);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    send_keyboard_release();
                    vTaskDelay(30 / portTICK_PERIOD_MS);
                }
            }
        }
        else if (strncmp(line, "m ", 2) == 0) {
            int dx, dy;
            if (sscanf(line + 2, "%d %d", &dx, &dy) == 2) {
                send_mouse_report(0, dx, dy, 0);
                ESP_LOGI(TAG, "Sent mouse move: dx=%d, dy=%d", dx, dy);
            }
        }
        else if (strncmp(line, "mc ", 3) == 0) {
            int btn = atoi(line + 3);
            send_mouse_report(btn, 0, 0, 0);
            ESP_LOGI(TAG, "Sent mouse click: btn=%d", btn);
        }
        else if (strncmp(line, "mu", 2) == 0) {
            send_mouse_report(0, 0, 0, 0);
            ESP_LOGI(TAG, "Sent mouse up");
        }
        else if (strncmp(line, "sc ", 3) == 0) {
            char key = line[3];
            uint8_t usage = 0;
            switch (key) {
                case 'v': usage = 0xE9; ESP_LOGI(TAG, "Volume Up"); break;
                case 'V': usage = 0xEA; ESP_LOGI(TAG, "Volume Down"); break;
                case 'm': usage = 0xE2; ESP_LOGI(TAG, "Mute"); break;
                case 'p': usage = 0xB0; ESP_LOGI(TAG, "Play"); break;
                case 'P': usage = 0xB1; ESP_LOGI(TAG, "Pause"); break;
                case 's': usage = 0xB7; ESP_LOGI(TAG, "Stop"); break;
                case 'n': usage = 0xB5; ESP_LOGI(TAG, "Next Track"); break;
                case 'b': usage = 0xB6; ESP_LOGI(TAG, "Previous Track"); break;
                default: ESP_LOGW(TAG, "Unknown consumer key: %c", key); break;
            }
            if (usage) {
                send_consumer_report(usage);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                send_consumer_report(0);
            }
        }
        else if (strncmp(line, "status", 6) == 0) {
            ESP_LOGI(TAG, "BLE Connected: %s", s_ble_connected ? "YES" : "NO");
        }
        else {
            ESP_LOGW(TAG, "Unknown command: %s", line);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// NimBLE host task - runs the host event loop
static void ble_hid_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi AP
    ESP_LOGI(TAG, "Initializing WiFi AP...");
    wifi_init();

    // Start HTTP server
    ESP_LOGI(TAG, "Starting HTTP server...");
    http_server_start();

#if CONFIG_BT_NIMBLE_ENABLED
    // BT controller must reach ENABLED state before esp_nimble_init():
    // esp_vhci_host_register_callback() returns ESP_FAIL otherwise.
    ESP_LOGI(TAG, "Initializing BT controller...");
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Initializing NimBLE...");
    ret = esp_nimble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Initializing BLE HID Device...");
    ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %d (0x%X) %s", ret, (unsigned)ret, esp_err_to_name(ret));
        return;
    }

    ret = esp_hidd_dev_battery_set(s_hid_dev, HID_BATTERY_LEVEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_battery_set failed: %s", esp_err_to_name(ret));
    }

    // Advertising is started from ESP_HIDD_START_EVENT instead: ble_gap_adv_start()
    // returns BLE_HS_ENOTSYNC until the NimBLE host is synced.
    ESP_LOGI(TAG, "Initializing BLE advertising...");
    esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, ble_hid_config.device_name);

    ESP_LOGI(TAG, "Starting NimBLE host task...");
    nimble_port_freertos_init(ble_hid_host_task);

    ESP_LOGI(TAG, "Starting interactive test task...");
    xTaskCreate(interactive_test_task, "interactive_test", 4096, NULL, configMAX_PRIORITIES - 3, NULL);

    ESP_LOGI(TAG, "Starting command queue task...");
    xTaskCreate(queue_task, "queue_task", 4096, NULL, configMAX_PRIORITIES - 4, NULL);

    ESP_LOGI(TAG, "BLE HID POC initialized. Device name: %s", ble_hid_config.device_name);
    ESP_LOGI(TAG, "Waiting for connection from host...");
#else
    ESP_LOGE(TAG, "NimBLE is not enabled! Please enable CONFIG_BT_NIMBLE_ENABLED");
#endif
}
