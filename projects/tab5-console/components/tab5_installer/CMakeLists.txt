idf_component_register(SRCS "tab5_installer.c"
                      INCLUDE_DIRS "include"
                      REQUIRES app_update esp_partition spi_flash nvs_flash fatfs mbedtls tab5_app_boot)
