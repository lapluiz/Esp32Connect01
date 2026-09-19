idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_gpio freertos led_strip esp_wifi esp_event nvs_flash esp_netif esp_http_server esp_http_client)