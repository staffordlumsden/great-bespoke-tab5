idf_component_register(SRCS "tab5_console.cpp" "matrix_clock.cpp"
                            "input_tab5_kbd.cpp" "hid_translate.cpp"
                      INCLUDE_DIRS "." "fonts"
                      REQUIRES tab5_hal tab5_radio nvs_flash app_update
                               esp_partition esp_psram spi_flash esp_wifi bt
                               tab5_installer tab5_weather_snapshot mbedtls
                               esp_netif esp_driver_i2c esp_driver_gpio esp_timer)
