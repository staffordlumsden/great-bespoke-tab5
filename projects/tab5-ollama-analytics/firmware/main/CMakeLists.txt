idf_component_register(
    SRCS "app_main.cpp" "ollama_analytics.cpp" "ollama_http.cpp" "ui.cpp"
         "cyd_usage/usage_model.cpp"
    INCLUDE_DIRS "." "cyd_usage" "../../../tab5-console/main/fonts"
    REQUIRES tab5_hal tab5_app_boot tab5_weather_wifi nvs_flash json
             esp_http_client esp_timer esp_netif lwip mbedtls esp-tls)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wframe-larger-than=4096 -Werror=frame-larger-than=4096 -fstack-usage)
