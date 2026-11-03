idf_component_register(
    SRCS
        "app_main.cpp"
        "connection.cpp"
        "cursor_renderer.cpp"
        "hid_translate.cpp"
        "input_tab5_kbd.cpp"
        "ssh_connection.cpp"
        "tab5_terminal_config.cpp"
        "tab5_terminal_display.cpp"
        "tofu.cpp"
    INCLUDE_DIRS "." "fonts"
    REQUIRES
        tab5_hal
        tab5_app_boot
        tab5_weather_wifi
        term_core
        libssh2_esp
        nvs_flash
        json
        esp_timer
        esp_netif
        esp_wifi
        lwip
        mbedtls
        esp_hosted
        esp_driver_i2c
        esp_driver_gpio
        usb_host_hid)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wformat=2
    -Wframe-larger-than=4096 -Werror=frame-larger-than=4096
    -fstack-usage)
