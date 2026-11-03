idf_component_register(SRCS "app_main.cpp" "camera_service.cpp" "photo_service.cpp" "photo_storage.cpp"
    "gallery_model.cpp" "gallery_image.cpp"
    INCLUDE_DIRS "." "../../../tab5-quicknote/firmware/main/fonts"
    REQUIRES tab5_hal tab5_app_boot nvs_flash esp_video esp_driver_i2c esp_driver_jpeg esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wformat=2 -Wframe-larger-than=4096 -Werror=frame-larger-than=4096)
