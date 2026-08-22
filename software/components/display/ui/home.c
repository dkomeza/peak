#include "home.h"

#include "lvgl.h"

static lv_obj_t *s_status_label;

static const char *action_name(display_action_t action) {
  switch (action) {
  case DISPLAY_ACTION_GEAR:
    return "Gear";
  case DISPLAY_ACTION_SUPPORT_MODE:
    return "Support mode";
  case DISPLAY_ACTION_RIDE_MODE:
    return "Ride mode";
  case DISPLAY_ACTION_WALK_MODE:
    return "Walk mode";
  case DISPLAY_ACTION_POWER:
    return "Power";
  default:
    return "Control";
  }
}

void display_home_create(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "PEAK");
  lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -24);

  s_status_label = lv_label_create(screen);
  lv_label_set_text(s_status_label, "Display ready");
  lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x8E8E93),
                              LV_PART_MAIN);
  lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 20);
}

void display_home_apply_event(const display_event_t *event) {
  if (event == NULL || s_status_label == NULL) {
    return;
  }

  switch (event->type) {
  case DISPLAY_EVENT_BOOT_STAGE:
    lv_label_set_text_fmt(s_status_label, "Boot stage %u",
                          event->data.boot.stage);
    break;
  case DISPLAY_EVENT_ACTION_RESULT:
    lv_label_set_text_fmt(s_status_label, "%s: %s",
                          action_name(event->data.action.action),
                          esp_err_to_name(event->data.action.result));
    break;
  case DISPLAY_EVENT_FAULT:
    lv_label_set_text_fmt(s_status_label, "Fault: %s",
                          esp_err_to_name(event->data.fault.code));
    break;
  default:
    break;
  }
}
