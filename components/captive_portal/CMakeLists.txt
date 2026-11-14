idf_component_register(
    SRCS "src/captive_portal.c" "src/portal_httpd.c" "src/portal_html.c"
         "src/portal_tz.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server clock_settings
)
