set(requires esp-tls nvs_flash esp_netif esp_http_server esp_driver_gpio esp_wifi esp_timer mdns app_update esp_pm)

idf_component_register(SRCS main.c wifi.c button.c webserver.c ota.c
                    PRIV_REQUIRES ${requires}
                    INCLUDE_DIRS "."
                    EMBED_FILES favicon.png
                    EMBED_TXTFILES remotes.index.html control.index.html)
