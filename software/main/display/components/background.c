#include "background.h"

#include "lvgl.h"

LV_IMAGE_DECLARE(blured_circle);

static void draw_grid_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_layer_t *layer = lv_event_get_layer(e);

  int32_t w = lv_obj_get_width(obj);
  int32_t h = lv_obj_get_height(obj);

  int32_t step = (w * 165) / 1000;
  if (step <= 0)
    return;

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);

  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_white();
  line_dsc.opa = 8;
  line_dsc.width = 2;

  // Draw vertical lines
  for (int32_t x = step; x < w; x += step) {
    line_dsc.p1.x = coords.x1 + x;
    line_dsc.p1.y = coords.y1;
    line_dsc.p2.x = coords.x1 + x;
    line_dsc.p2.y = coords.y2;
    lv_draw_line(layer, &line_dsc);
  }

  // Draw horizontal lines
  for (int32_t y = step; y < h; y += step) {
    line_dsc.p1.x = coords.x1;
    line_dsc.p1.y = coords.y1 + y;
    line_dsc.p2.x = coords.x2;
    line_dsc.p2.y = coords.y1 + y;
    lv_draw_line(layer, &line_dsc);
  }
}

background_view_t background_create(lv_obj_t *parent, int32_t width,
                                    int32_t height) {
  lv_color_t bg_color = lv_color_hex(0x000000);

  lv_obj_t *bg = lv_obj_create(parent);
  lv_obj_set_size(bg, width, height);

  lv_obj_set_style_bg_color(bg, bg_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);

  lv_obj_set_style_border_width(bg, 26, LV_PART_MAIN);
  lv_obj_set_style_border_color(bg, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(bg, 216,
                              LV_PART_MAIN); // 85% opacity (255 * 0.85 ≈ 216)
  lv_obj_set_style_border_side(bg, LV_BORDER_SIDE_INTERNAL, LV_PART_MAIN);

  lv_obj_add_event_cb(bg, draw_grid_cb, LV_EVENT_DRAW_MAIN, NULL);

  lv_obj_t *glow = lv_image_create(bg);

  lv_image_set_src(glow, &blured_circle);

  lv_obj_remove_flag(glow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(glow, 555, 555);
  lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, 0);

  lv_color_t accent_color = lv_color_hex(0x007AFF);
  lv_obj_set_style_image_recolor(glow, accent_color, LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(glow, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_set_style_opa(glow, 32, LV_PART_MAIN); // 16% of 255 = 41

  return (background_view_t){
      .root = bg,
      .glow = glow,
  };
}
