#include "metric_card.h"

metric_card_t metric_card_create(lv_obj_t *parent, const char *title,
                                 lv_align_t align, int32_t x_offset,
                                 int32_t y_offset) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 200, 112);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x17171A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
  lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(card, align, x_offset, y_offset);

  lv_obj_t *caption = lv_label_create(card);
  lv_label_set_text(caption, title);
  lv_obj_set_style_text_color(caption, lv_color_hex(0x8E8E93), LV_PART_MAIN);
  lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, 0);

  metric_card_t result = {
      .value = lv_label_create(card),
      .detail = lv_label_create(card),
  };
  lv_obj_set_style_text_color(result.value, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(result.value, LV_ALIGN_BOTTOM_LEFT, 0, -18);
  lv_obj_set_style_text_color(result.detail, lv_color_hex(0x8E8E93),
                              LV_PART_MAIN);
  lv_obj_align(result.detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  return result;
}
