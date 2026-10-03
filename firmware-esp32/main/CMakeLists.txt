idf_component_register(
    SRCS "app_main.c" "gl30_settings_store.c" "../bsp/kk_oled_driver.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../bsp" "../components/kk_oled/driver" "../components/kk_oled/graphics"
    REQUIRES gl30_board gl30_ui gl30_motor kk_oled kk_ui esp_timer esp_driver_usb_serial_jtag nvs_flash
    WHOLE_ARCHIVE
)
