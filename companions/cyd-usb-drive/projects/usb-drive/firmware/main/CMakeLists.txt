idf_component_register(SRCS "app_main.cpp" "console_bridge.cpp" "drive_path.cpp" "serial_drive.cpp" "ui.cpp"
    INCLUDE_DIRS "." "../../../launcher/firmware/main"
    REQUIRES cyd_hal nvs_flash app_update esp_app_format driver fatfs json mbedtls)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wframe-larger-than=2048 -Werror=frame-larger-than=2048 -fstack-usage)
