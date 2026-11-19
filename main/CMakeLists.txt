idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hu058 clock_fx clock_sensors clock_time buzzer buttons buttons_hal
             clock_settings clock_ntp wifi_manager captive_portal web_server status_led
             app_update esp_timer nvs_flash esp_netif esp_event
             esp_driver_gptimer
)
