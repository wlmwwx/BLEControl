idf_component_register(SRCS "ble_hid_poc.c" "esp_hid_gap.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_hid esp_netif esp_http_server)
