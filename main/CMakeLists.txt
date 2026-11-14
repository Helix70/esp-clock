idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES hu058 buttons buttons_hal clock_settings clock_ntp
             wifi_manager captive_portal status_led esp_timer nvs_flash
             esp_netif esp_event esp_driver_gpio esp_driver_gptimer esp_adc
)
