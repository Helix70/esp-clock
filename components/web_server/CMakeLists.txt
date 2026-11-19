idf_component_register(
    SRCS "src/web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server clock_settings ota_server
    PRIV_REQUIRES settings_form
)
