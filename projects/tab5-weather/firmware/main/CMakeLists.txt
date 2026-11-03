idf_component_register(
    SRCS
        "tab5_weather.cpp"
        "console_bridge.cpp"
        "weather_model.cpp"
        "weather_http.cpp"
        "ui.cpp"
    INCLUDE_DIRS "." "fonts"
    REQUIRES
        tab5_hal
        tab5_app_boot
        tab5_weather_snapshot
        tab5_weather_wifi
        nvs_flash
        app_update
        esp_app_format
        esp_partition
        esp_http_client
        esp_netif
        json
        esp_timer
        lwip
        mbedtls
        esp-tls)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wframe-larger-than=4096
    -Werror=frame-larger-than=4096
    -fstack-usage)
