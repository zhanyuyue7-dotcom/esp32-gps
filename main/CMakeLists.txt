idf_component_register(
  SRCS
    "main.cpp"
    "Ssd1306.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    GpsTrackerCore
    esp_driver_gpio
    esp_driver_spi
    esp_driver_uart
    esp_event
    esp_http_server
    esp_netif
    esp_timer
    esp_wifi
    nvs_flash
    spiffs
  EMBED_TXTFILES "web_page.html"
)
