idf_component_register(SRCS
  "src/kk_ui.c" "src/kk_ui_anim.c" "src/kk_ui_dialog.c"
  "src/kk_ui_draw.c" "src/kk_ui_home.c" "src/kk_ui_menu.c"
  "src/kk_ui_nav.c" "src/kk_ui_page.c"
  INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src" REQUIRES kk_oled)
