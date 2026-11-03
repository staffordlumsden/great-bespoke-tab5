idf_component_register(
    SRCS "tab5_radio.c" "tab5_bluetooth.cpp"
         "cyd_keyboard/keyboard_mapping.cpp" "cyd_keyboard/keyboard_report.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_event esp_netif esp_wifi bt
    PRIV_INCLUDE_DIRS "cyd_keyboard"
    PRIV_REQUIRES esp_timer freertos esp_hid esp_hosted)
