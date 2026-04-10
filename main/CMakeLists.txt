idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS ""
                    PRIV_REQUIRES esp_driver_gpio
                    REQUIRES led_strip
                    )


