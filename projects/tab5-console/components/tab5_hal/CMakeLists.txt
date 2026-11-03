idf_component_register(SRCS "tab5_hal.c" "tab5_keyboard.c"
                     INCLUDE_DIRS "include"
                     REQUIRES esp_lcd esp_psram sdmmc fatfs
                     PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_sdmmc
                                   esp_driver_ledc esp_timer esp_mm esp_lcd_st7121
                                   usb usb_host_hid)
