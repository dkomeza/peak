#include "home.h"

#include "metric_card.h"
#include "lvgl.h"

typedef struct {
  lv_obj_t *speed;
  lv_obj_t *status;
  metric_card_t battery;
  metric_card_t power;
  metric_card_t gear;
  metric_card_t mode;
} home_view_t;

static home_view_t s_view;

static const char *support_mode_name(uint8_t mode) {
  static const char *const names[] = {"PAS", "TORQUE"};
  return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "UNKNOWN";
}

static const char *ride_mode_name(uint8_t mode) {
  static const char *const names[] = {"NORMAL", "MOUNTAIN"};
  return mode < sizeof(names) / sizeof(names[0]) ? names[mode] : "UNKNOWN";
}

static void set_metric(lv_obj_t *value, lv_obj_t *detail, const char *text,
                       const char *unit) {
  lv_label_set_text(value, text);
  lv_label_set_text(detail, unit);
}

void display_home_create(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "PEAK");
  lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

  s_view.speed = lv_label_create(screen);
  lv_label_set_text(s_view.speed, "--.- km/h");
  lv_obj_set_style_text_color(s_view.speed, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(s_view.speed, LV_ALIGN_TOP_MID, 0, 92);

  s_view.status = lv_label_create(screen);
  lv_label_set_text(s_view.status, "Waiting for ESC telemetry");
  lv_obj_set_style_text_color(s_view.status, lv_color_hex(0x8E8E93),
                              LV_PART_MAIN);
  lv_obj_align(s_view.status, LV_ALIGN_TOP_MID, 0, 128);

  s_view.battery = metric_card_create(screen, "BATTERY", LV_ALIGN_CENTER,
                                      -105, 24);
  s_view.power = metric_card_create(screen, "POWER", LV_ALIGN_CENTER, 105, 24);
  s_view.gear = metric_card_create(screen, "ASSIST", LV_ALIGN_CENTER, -105,
                                   148);
  s_view.mode = metric_card_create(screen, "MODE", LV_ALIGN_CENTER, 105, 148);

  set_metric(s_view.battery.value, s_view.battery.detail, "--", "%");
  set_metric(s_view.power.value, s_view.power.detail, "--", "W");
  set_metric(s_view.gear.value, s_view.gear.detail, "--", "gear");
  set_metric(s_view.mode.value, s_view.mode.detail, "--", "support");
}

void display_home_apply_event(const display_event_t *event) {
  if (event == NULL || s_view.status == NULL) {
    return;
  }

  switch (event->type) {
  case DISPLAY_EVENT_ACTION_RESULT:
    lv_label_set_text(s_view.status,
                      event->data.action.result == ESP_OK ? "Command sent"
                                                          : "Command failed");
    break;
  case DISPLAY_EVENT_FAULT:
    lv_label_set_text_fmt(s_view.status, "Fault: %s",
                          esp_err_to_name(event->data.fault.code));
    break;
  default:
    break;
  }
}

void display_home_update(const display_ui_model_t *model) {
  if (model == NULL || s_view.speed == NULL) {
    return;
  }

  if (model->has_speed) {
    lv_label_set_text_fmt(s_view.speed, "%.1f km/h", (double)model->speed_kph);
  }
  if (model->has_power) {
    lv_label_set_text_fmt(s_view.power.value, "%d", model->power_w);
  }
  if (model->has_battery_percent) {
    lv_label_set_text_fmt(s_view.battery.value, "%u", model->battery_percent);
  }
  if (model->has_battery_voltage) {
    lv_label_set_text_fmt(s_view.battery.detail, "%.1f V",
                          (double)model->battery_voltage_v);
  }
  if (model->has_gear) {
    lv_label_set_text_fmt(s_view.gear.value, "%u", model->gear);
    lv_label_set_text(s_view.gear.detail,
                      model->walk_active ? "walk active" : "gear");
  }
  if (model->has_ride_mode) {
    if (model->has_support_mode) {
      lv_label_set_text(s_view.mode.value,
                        support_mode_name(model->support_mode));
      lv_label_set_text(s_view.mode.detail, ride_mode_name(model->ride_mode));
    } else {
      lv_label_set_text(s_view.mode.value, ride_mode_name(model->ride_mode));
      lv_label_set_text(s_view.mode.detail, "ride mode");
    }
  }
  if (model->has_motor_temp && model->has_controller_temp) {
    lv_label_set_text_fmt(s_view.status, "Motor %d C  Controller %d C",
                          model->motor_temp_c, model->controller_temp_c);
  } else if (model->has_motor_temp) {
    lv_label_set_text_fmt(s_view.status, "Motor %d C", model->motor_temp_c);
  } else if (model->has_controller_temp) {
    lv_label_set_text_fmt(s_view.status, "Controller %d C",
                          model->controller_temp_c);
  }
}
