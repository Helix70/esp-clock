idf_component_register(
    SRCS "src/settings_form_html.c" "src/settings_form_parse.c"
         "src/settings_form_tz.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_app_format clock_settings
)
