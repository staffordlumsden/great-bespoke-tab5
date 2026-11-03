Source and header copied without changes from M5Stack/M5Tab5-UserDemo commit
b4e356bc491ca070d54004718dad789c07d5fc93:
platforms/tab5/components/esp_lcd_st7121/.
https://github.com/m5stack/M5Tab5-UserDemo

Only CMakeLists.txt differs: use an ordinary ESP-IDF component registration
instead of the factory demo's package_manager helpers. Apache-2.0 source
notices are retained. Board-specific timing is supplied by tab5_hal.
