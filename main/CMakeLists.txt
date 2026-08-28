idf_component_register(SRCS "ble_hid_poc.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_hid)
