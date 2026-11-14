idf_component_register(
    SRCS "src/aip33628_hal_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_hal_gpio
)
