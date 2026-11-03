cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32p4")
set(PROJECT_VER "0.1.1")
set(TAB5_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../tab5-console/components")
set(EXTRA_COMPONENT_DIRS
    "${TAB5_COMPONENTS}/tab5_hal"
    "${TAB5_COMPONENTS}/tab5_app_boot"
    "${TAB5_COMPONENTS}/esp_lcd_st7121"
    "${TAB5_COMPONENTS}/fatfs")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Tab5HtmlViewer)

# An SD application must never replace the resident factory Console.
add_custom_target(tab5_htmlviewer_sd_only
    COMMAND ${CMAKE_COMMAND} -E echo "Tab5HtmlViewer is an SD app; package it and launch it from Console."
    COMMAND ${CMAKE_COMMAND} -E false)
foreach(flash_target flash app-flash bootloader-flash partition-table-flash)
    if(TARGET ${flash_target})
        add_dependencies(${flash_target} tab5_htmlviewer_sd_only)
    endif()
endforeach()
