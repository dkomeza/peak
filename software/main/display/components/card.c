#include "card.h"

// Event callback to handle the "cqw" relative sizing (padding, radius, shadow)
static void glass_card_resize_cb(lv_event_t *e) {
  lv_obj_t *card = lv_event_get_target(e);
  lv_obj_t *parent = lv_obj_get_parent(card);

  int32_t parent_w = lv_obj_get_width(parent);
  if (parent_w <= 0)
    return;

  // padding: 3.4cqw (top/bottom), 3.6cqw (left/right)
  int32_t pad_v = (parent_w * 34) / 1000;
  int32_t pad_h = (parent_w * 36) / 1000;
  lv_obj_set_style_pad_top(card, pad_v, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(card, pad_v, LV_PART_MAIN);
  lv_obj_set_style_pad_left(card, pad_h, LV_PART_MAIN);
  lv_obj_set_style_pad_right(card, pad_h, LV_PART_MAIN);

  // border-radius: 3.2cqw
  int32_t radius = (parent_w * 32) / 1000;
  lv_obj_set_style_radius(card, radius, LV_PART_MAIN);

  // box-shadow: 0 1.5cqw 3cqw rgba(0, 0, 0, 0.24);
  // Y-offset is 1.5cqw, Spread/Blur is 3cqw
  int32_t shadow_y = (parent_w * 15) / 1000;
  int32_t shadow_w = (parent_w * 30) / 1000;

  // HARD LIMIT to prevent Task Watchdog crash!
  // Software shadow rendering over 24px will freeze the CPU.
  if (shadow_w > 24)
    shadow_w = 24;

  lv_obj_set_style_shadow_ofs_y(card, shadow_y, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, shadow_w, LV_PART_MAIN);
}

// Function to create the Glass Card
lv_obj_t *glass_card_create(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);

  // overflow: hidden; (Clips children to the border radius and hides
  // scrollbars)
  lv_obj_set_style_clip_corner(card, true, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // display: flex; flex-direction: column; justify-content: space-between;
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

  // flex_align(obj, main_axis_align, cross_axis_align, track_align)
  lv_obj_set_flex_align(card,
                        LV_FLEX_ALIGN_SPACE_BETWEEN, // justify-content
                        LV_FLEX_ALIGN_START,         // align-items (default)
                        LV_FLEX_ALIGN_START);        // align-content

  // border: 1px solid var(--screen-line);
  lv_color_t screen_line = lv_color_hex(0x333333); // Replace with your hex
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, screen_line, LV_PART_MAIN);

  // background: var(--screen-card);
  lv_color_t screen_card = lv_color_hex(0x1A1A1A); // Replace with your hex
  lv_obj_set_style_bg_color(card, screen_card, LV_PART_MAIN);

  // Faking the glass effect (opacity between ~60% to 85%)
  lv_obj_set_style_bg_opa(card, 180, LV_PART_MAIN); // 180 / 255 ≈ 70%

  // box-shadow: base parameters
  lv_obj_set_style_shadow_color(card, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 61, LV_PART_MAIN); // 24% of 255 = 61
  lv_obj_set_style_shadow_ofs_x(card, 0, LV_PART_MAIN);

  // Attach resize callback to calculate cqw units dynamically
  lv_obj_add_event_cb(card, glass_card_resize_cb, LV_EVENT_SIZE_CHANGED, NULL);

  return card;
}

lv_obj_t *metric_card_create(lv_obj_t *parent,
                             const metric_card_props_t *props) {
  if (parent == NULL || props == NULL || props->value == NULL) {
    return NULL;
  }

  lv_obj_t *card = glass_card_create(parent);

  lv_obj_t *icon = lv_label_create(card);
  lv_label_set_text(icon, props->icon != NULL ? props->icon : "");
  lv_obj_set_style_text_color(icon, lv_color_hex(0x888888), 0);

  lv_obj_t *value = lv_label_create(card);
  lv_label_bind_text(value, props->value, props->value_format);
  lv_obj_set_style_text_color(value, lv_color_white(), 0);

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, props->label != NULL ? props->label : "");
  lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);

  if (props->status != NULL) {
    lv_obj_t *status = lv_label_create(card);
    lv_label_bind_text(status, props->status, NULL);
    lv_obj_set_style_text_color(status, lv_color_hex(0xFF9500), 0);
  }

  return card;
}
