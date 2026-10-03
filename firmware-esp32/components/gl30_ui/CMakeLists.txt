idf_component_register(SRCS
  "src/gl30_model.c" "src/gl30_preferences.c" "src/gl30_gesture.c" "src/gl30_demo.c" "src/gl30_render.c" "src/gl30_type.c"
  "assets/gl30_type_data.c"
  "assets/gl30_font_body.c" "assets/gl30_font_title.c" "assets/gl30_font_digits.c" "assets/gl30_font_small.c"
  "assets/gl30_summit.c"
  INCLUDE_DIRS "include" REQUIRES kk_ui kk_oled gl30_motor)
