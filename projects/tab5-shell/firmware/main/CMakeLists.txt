idf_component_register(
    SRCS
        "app_main.cpp"
        "file_store.cpp"
        "document_mode.cpp"
        "session.cpp"
        "package_runtime.cpp"
        "lua_runtime.cpp"
        "download.cpp"
        "shell_network.cpp"
        "ssh_connection.cpp"
        "cursor_renderer.cpp"
        "scrollback.cpp"
        "hid_translate.cpp"
        "input_tab5_kbd.cpp"
        "line_editor.cpp"
        "shell_core.cpp"
        "shell_display.cpp"
    INCLUDE_DIRS "." "${SHARED_FONTS}" "../../../tab5-terminal/firmware/main"
    REQUIRES
        tab5_hal
        tab5_app_boot
        term_core
        nvs_flash
        esp_driver_i2c
        esp_driver_gpio
        esp_timer
        usb_host_hid
        fatfs
        sdmmc
        libssh2_esp esp_wifi esp_netif lwip mbedtls esp_hosted lua54 esp_http_client)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wformat=2
    -Wframe-larger-than=4096 -Werror=frame-larger-than=4096
    -fstack-usage)
