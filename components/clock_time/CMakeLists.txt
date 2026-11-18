idf_component_register(
    SRCS "src/clock_time.c"
    INCLUDE_DIRS "include"
    REQUIRES ds1302 clock_ntp esp_timer
)
