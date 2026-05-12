#include "ui/style.h"

#define PEAK_RADIUS_CARD 12
#define PEAK_RADIUS_PILL 18

void peak_ui_style_init(void) {}

lv_color_t peak_ui_color_bg(void) { return lv_color_hex(0x070A0C); }
lv_color_t peak_ui_color_panel(void) { return lv_color_hex(0x151B1E); }
lv_color_t peak_ui_color_panel_alt(void) { return lv_color_hex(0x101517); }
lv_color_t peak_ui_color_text(void) { return lv_color_hex(0xF4F6F7); }
lv_color_t peak_ui_color_muted(void) { return lv_color_hex(0x8B9298); }
lv_color_t peak_ui_color_accent(void) { return lv_color_hex(0x16D9A1); }
lv_color_t peak_ui_color_warm(void) { return lv_color_hex(0xFF9F2E); }

void peak_ui_style_screen(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, peak_ui_color_bg(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(obj, 16, 0);
}

void peak_ui_style_card(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, peak_ui_color_panel(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x2A3337), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, PEAK_RADIUS_CARD, 0);
  lv_obj_set_style_pad_all(obj, 14, 0);
}

void peak_ui_style_pill(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, peak_ui_color_panel_alt(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x2A3337), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_radius(obj, PEAK_RADIUS_PILL, 0);
  lv_obj_set_style_pad_hor(obj, 14, 0);
  lv_obj_set_style_pad_ver(obj, 8, 0);
}

void peak_ui_style_label(lv_obj_t *obj, lv_color_t color,
                         const lv_font_t *font) {
  lv_obj_set_style_text_color(obj, color, 0);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_letter_space(obj, 0, 0);
}
