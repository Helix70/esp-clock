idf_component_register(
    SRCS "src/buttons_hal_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES buttons esp_driver_gpio esp_timer
)
