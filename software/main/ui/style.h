#ifndef PEAK_UI_STYLE_H
#define PEAK_UI_STYLE_H

#include "lvgl.h"

void peak_ui_style_init(void);

lv_color_t peak_ui_color_bg(void);
lv_color_t peak_ui_color_panel(void);
lv_color_t peak_ui_color_panel_alt(void);
lv_color_t peak_ui_color_text(void);
lv_color_t peak_ui_color_muted(void);
lv_color_t peak_ui_color_accent(void);
lv_color_t peak_ui_color_warm(void);

void peak_ui_style_screen(lv_obj_t *obj);
void peak_ui_style_card(lv_obj_t *obj);
void peak_ui_style_pill(lv_obj_t *obj);
void peak_ui_style_label(lv_obj_t *obj, lv_color_t color,
                         const lv_font_t *font);

#endif
