cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4")
set(PROJECT_VER "0.1.3")

set(TAB5_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-console/components")
set(TERMINAL_CORE "${CMAKE_CURRENT_LIST_DIR}/../../tab5-terminal/firmware/components/term_core")
set(SHARED_FONTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-quicknote/firmware/main/fonts")
set(EXTRA_COMPONENT_DIRS
    "${TAB5_COMPONENTS}/tab5_hal"
    "${TAB5_COMPONENTS}/tab5_app_boot"
    "${TAB5_COMPONENTS}/esp_lcd_st7121"
    "${TAB5_COMPONENTS}/fatfs"
    "${CMAKE_CURRENT_LIST_DIR}/../../tab5-terminal/firmware/components/libssh2_esp"
    "${TERMINAL_CORE}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Tab5Shell)

# The image belongs in an SD application slot; never overwrite resident Console.
add_custom_target(tab5_shell_sd_only
    COMMAND ${CMAKE_COMMAND} -E echo
            "Tab5 Shell is an SD app; package it and launch it from Console."
    COMMAND ${CMAKE_COMMAND} -E false)
foreach(flash_target flash app-flash bootloader-flash partition-table-flash)
    if(TARGET ${flash_target})
        add_dependencies(${flash_target} tab5_shell_sd_only)
    endif()
endforeach()
