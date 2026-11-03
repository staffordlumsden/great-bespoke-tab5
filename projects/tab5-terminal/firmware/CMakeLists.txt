cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4")
set(PROJECT_VER "0.1.4")

set(TAB5_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-console/components")
set(EXTRA_COMPONENT_DIRS
    "${TAB5_COMPONENTS}/tab5_hal"
    "${TAB5_COMPONENTS}/tab5_app_boot"
    "${TAB5_COMPONENTS}/esp_lcd_st7121"
    "${TAB5_COMPONENTS}/fatfs"
    "${CMAKE_CURRENT_LIST_DIR}/../../tab5-weather/components/tab5_weather_wifi")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Tab5Terminal)

# This image belongs in an SD application slot.  Blocking the normal flash
# targets prevents an accidental overwrite of the resident factory Console.
add_custom_target(tab5_terminal_sd_only
    COMMAND ${CMAKE_COMMAND} -E echo
            "Tab5 Terminal is an SD app; package it and launch it from Console."
    COMMAND ${CMAKE_COMMAND} -E false)
foreach(flash_target flash app-flash bootloader-flash partition-table-flash)
    if(TARGET ${flash_target})
        add_dependencies(${flash_target} tab5_terminal_sd_only)
    endif()
endforeach()
