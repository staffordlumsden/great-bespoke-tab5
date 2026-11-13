idf_component_register(SRCS "app_main.cpp" "input_tab5_kbd.cpp" "hid_translate.cpp" "../../src/calendar_view.cpp" "../../src/calendar_files.cpp" "../../src/ical_lines.cpp" "../../src/calendar_events.cpp" "../../src/calendar_feeds.cpp" "../../src/calendar_fetch.cpp"
    INCLUDE_DIRS "." "../../src" REQUIRES tab5_hal tab5_app_boot tab5_radio nvs_flash esp_driver_i2c esp_driver_gpio esp_timer usb_host_hid fatfs sdmmc esp_http_client esp_netif mbedtls esp-tls)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wframe-larger-than=4096 -Werror=frame-larger-than=4096)
