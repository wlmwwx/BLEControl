/*
 * ESP HID GAP - BLE Advertising Header
 */

#ifndef _ESP_HID_GAP_H_
#define _ESP_HID_GAP_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_hid_ble_gap_adv_init(uint16_t appearance, const char *device_name);
esp_err_t esp_hid_ble_gap_adv_start(void);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_HID_GAP_H_ */
