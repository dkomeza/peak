#include "ui/style.h"

void peak_ui_style_init(void) {}

lv_color_t peak_ui_color_bg(void) { return lv_color_hex(0x070A0C); }
lv_color_t peak_ui_color_panel(void) { return lv_color_hex(0x151B1E); }
lv_color_t peak_ui_color_panel_alt(void) { return lv_color_hex(0x101517); }
lv_color_t peak_ui_color_text(void) { return lv_color_hex(0xF4F6F7); }
lv_color_t peak_ui_color_muted(void) { return lv_color_hex(0x8B9298); }
lv_color_t peak_ui_color_accent(void) { return lv_color_hex(0x16D9A1); }
lv_color_t peak_ui_color_warm(void) { return lv_color_hex(0xFF9F2E); }

void peak_ui_style_screen(lv_obj_t *obj) { (void)obj; }
void peak_ui_style_card(lv_obj_t *obj) { (void)obj; }
void peak_ui_style_pill(lv_obj_t *obj) { (void)obj; }
void peak_ui_style_label(lv_obj_t *obj, lv_color_t color,
                         const lv_font_t *font) {
  (void)obj;
  (void)color;
  (void)font;
}
