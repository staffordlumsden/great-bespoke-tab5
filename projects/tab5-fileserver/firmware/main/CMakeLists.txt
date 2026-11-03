idf_component_register(
    SRCS
        "app_main.cpp"
        "fs_core.cpp"
        "fileserver_net.cpp"
        "fileserver_http.cpp"
        "fileserver_ui.cpp"
        "input_tab5_kbd.cpp"
        "hid_translate.cpp"
    INCLUDE_DIRS "." "fonts"
    REQUIRES
        tab5_hal
        tab5_app_boot
        nvs_flash
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_driver_i2c
        esp_driver_gpio
        usb_host_hid
        fatfs
        sdmmc
        lwip
        json)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wformat=2
    -Wframe-larger-than=4096 -Werror=frame-larger-than=4096
    -fstack-usage)
