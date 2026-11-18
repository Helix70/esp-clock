idf_component_register(
    SRCS "src/ds1302.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_rom
)
