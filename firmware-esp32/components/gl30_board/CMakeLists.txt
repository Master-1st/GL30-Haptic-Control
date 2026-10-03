idf_component_register(
    SRCS "gl30_board.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_spi freertos
    PRIV_REQUIRES esp_lcd_sh8601 esp_lcd_touch_cst820 heap log esp_timer esp_hw_support
)
