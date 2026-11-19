cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4")
set(PROJECT_VER "0.1.3-dev")

set(TAB5_CONSOLE_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-console/components")
set(TAB5_TERMINAL_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-terminal/firmware/components")
set(TAB5_WEATHER_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-weather/components")
set(TAB5_MEDIA_LIBS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-media-player/esp-devkit/libs")

set(EXTRA_COMPONENT_DIRS
    "${TAB5_CONSOLE_COMPONENTS}/tab5_hal"
    "${TAB5_CONSOLE_COMPONENTS}/tab5_app_boot"
    "${TAB5_CONSOLE_COMPONENTS}/esp_lcd_st7121"
    "${TAB5_CONSOLE_COMPONENTS}/fatfs"
    "${TAB5_WEATHER_COMPONENTS}/tab5_weather_wifi"
    "${TAB5_TERMINAL_COMPONENTS}/libssh2_esp"
    "${TAB5_MEDIA_LIBS}/compression_framework"
    "${TAB5_MEDIA_LIBS}/image_framework")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Tab5ThinClient)

# This image belongs in an SD application OTA slot; never overwrite Console.
add_custom_target(tab5_thinclient_sd_only
    COMMAND ${CMAKE_COMMAND} -E echo
            "Tab5ThinClient is an SD app; package it and launch it from Console."
    COMMAND ${CMAKE_COMMAND} -E false)
foreach(flash_target flash app-flash bootloader-flash partition-table-flash)
    if(TARGET ${flash_target})
        add_dependencies(${flash_target} tab5_thinclient_sd_only)
    endif()
endforeach()
