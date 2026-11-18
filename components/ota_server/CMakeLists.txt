idf_component_register(
    SRCS "src/ota_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server app_update esp_timer
)
