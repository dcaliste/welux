set(requires esp-tls nvs_flash esp_netif esp_http_server esp_driver_gpio esp_wifi esp_driver_gptimer mdns)

idf_component_register(SRCS main.c
                    PRIV_REQUIRES ${requires}
                    INCLUDE_DIRS "."
                    EMBED_FILES favicon.png
                    EMBED_TXTFILES velux.index.html)
