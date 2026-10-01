#include "home.h"

#include <math.h>
#include <stdio.h>

#define DISPLAY_HOME_BATTERY_WIDTH 104
#define DISPLAY_HOME_BATTERY_HEIGHT 40
#define DISPLAY_HOME_BATTERY_TERMINAL_WIDTH 7
#define DISPLAY_HOME_BATTERY_TERMINAL_HEIGHT 16
#define DISPLAY_HOME_GEAR_WIDTH 58
#define DISPLAY_HOME_GEAR_HEIGHT 24
#define DISPLAY_HOME_GEAR_GAP 12
#define DISPLAY_HOME_SPEED_SCALE 512
#define DISPLAY_HOME_TEMP_PULSE_DURATION_MS 600

#define COLOR_BACKGROUND 0x000000
#define COLOR_TEXT 0xF4F4F5
#define COLOR_MUTED 0x8E8E93
#define COLOR_GEAR_INACTIVE 0x27292E
#define COLOR_GEAR_ACTIVE 0x10B981
#define COLOR_TEMP_COLD 0x7DD3FC
#define COLOR_TEMP_NORMAL 0x22C55E
#define COLOR_TEMP_WARM 0xF97316
#define COLOR_TEMP_HOT 0xEF4444

static void make_transparent(lv_obj_t *obj) {
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

static void set_gear_active(lv_obj_t *gear, bool active) {
  lv_obj_set_style_bg_color(
      gear, lv_color_hex(active ? COLOR_GEAR_ACTIVE : COLOR_GEAR_INACTIVE),
      LV_PART_MAIN);
}

static bool speed_from_state(const display_state_t *state, int32_t *speed_kph) {
  if ((state->valid_fields & DISPLAY_STATE_SPEED) == 0 ||
      !isfinite(state->speed_kph) || state->speed_kph < 0.0f ||
      state->speed_kph > 999.0f) {
    return false;
  }

  *speed_kph = (int32_t)(state->speed_kph + 0.5f);
  return true;
}

static bool voltage_from_state(const display_state_t *state,
                               int32_t *voltage_tenths) {
  if ((state->valid_fields & DISPLAY_STATE_BATTERY_VOLTAGE) == 0 ||
      !isfinite(state->battery_voltage_v) || state->battery_voltage_v < 0.0f ||
      state->battery_voltage_v > 999.9f) {
    return false;
  }

  *voltage_tenths = (int32_t)(state->battery_voltage_v * 10.0f + 0.5f);
  return true;
}

static bool temperature_from_state(const display_state_t *state,
                                   uint32_t valid_field, int8_t temperature_c) {
  if ((state->valid_fields & valid_field) == 0 || temperature_c < -40) {
    return false;
  }

  return true;
}

static bool gear_from_state(const display_state_t *state, uint8_t *gear) {
  if ((state->valid_fields & DISPLAY_STATE_GEAR) == 0 || state->gear == 0 ||
      state->gear > DISPLAY_HOME_GEAR_COUNT) {
    return false;
  }

  *gear = state->gear;
  return true;
}

static bool support_mode_from_state(const display_state_t *state,
                                    display_support_mode_t *support_mode) {
  if ((state->valid_fields & DISPLAY_STATE_SUPPORT_MODE) == 0 ||
      state->support_mode > DISPLAY_SUPPORT_MODE_TORQUE) {
    return false;
  }

  *support_mode = state->support_mode;
  return true;
}

static void update_support_mode(display_home_t *home,
                                const display_state_t *state) {
  display_support_mode_t support_mode;
  bool valid = support_mode_from_state(state, &support_mode);
  if (home->support_mode_valid == valid &&
      (!valid || home->support_mode_display == support_mode)) {
    return;
  }

  if (valid) {
    lv_label_set_text(home->support_mode,
                      support_mode == DISPLAY_SUPPORT_MODE_TORQUE ? "TORQUE"
                                                                  : "PAS");
    home->support_mode_display = support_mode;
  } else {
    lv_label_set_text(home->support_mode, "--");
  }
  home->support_mode_valid = valid;
}

static void update_speed(display_home_t *home, const display_state_t *state) {
  int32_t speed_kph;
  bool valid = speed_from_state(state, &speed_kph);
  if (home->speed_valid == valid &&
      (!valid || home->speed_display_kph == speed_kph)) {
    return;
  }

  if (valid) {
    char text[8];
    snprintf(text, sizeof(text), "%ld", (long)speed_kph);
    lv_label_set_text(home->speed, text);
    home->speed_display_kph = speed_kph;
  } else {
    lv_label_set_text(home->speed, "--");
  }
  home->speed_valid = valid;
}

static void update_battery_voltage(display_home_t *home,
                                   const display_state_t *state) {
  int32_t voltage_tenths;
  bool valid = voltage_from_state(state, &voltage_tenths);
  if (home->battery_voltage_valid == valid &&
      (!valid || home->battery_voltage_tenths == voltage_tenths)) {
    return;
  }

  if (valid) {
    char text[20];
    snprintf(text, sizeof(text), "%ld.%ld V", (long)(voltage_tenths / 10),
             (long)(voltage_tenths % 10));
    lv_label_set_text(home->battery_voltage, text);
    home->battery_voltage_tenths = voltage_tenths;
  } else {
    lv_label_set_text(home->battery_voltage, "--.- V");
  }
  home->battery_voltage_valid = valid;
}

static void set_temperature_opacity(void *target, int32_t opacity) {
  lv_obj_set_style_text_opa(target, (lv_opa_t)opacity, LV_PART_MAIN);
}

static void apply_temperature_style(lv_obj_t *label, bool valid,
                                    int8_t temperature_c) {
  lv_anim_delete(label, set_temperature_opacity);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);

  if (!valid) {
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED),
                                LV_PART_MAIN);
    return;
  }

  uint32_t color = COLOR_TEMP_COLD;
  if (temperature_c >= 100) {
    color = COLOR_TEMP_HOT;
  } else if (temperature_c >= 90) {
    color = COLOR_TEMP_HOT;
  } else if (temperature_c >= 80) {
    color = COLOR_TEMP_WARM;
  } else if (temperature_c >= 20) {
    color = COLOR_TEMP_NORMAL;
  }
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);

  if (temperature_c < 100) {
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, label);
  lv_anim_set_exec_cb(&animation, set_temperature_opacity);
  lv_anim_set_values(&animation, LV_OPA_40, LV_OPA_COVER);
  lv_anim_set_duration(&animation, DISPLAY_HOME_TEMP_PULSE_DURATION_MS);
  lv_anim_set_reverse_duration(&animation, DISPLAY_HOME_TEMP_PULSE_DURATION_MS);
  lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&animation);
}

static void update_temperature(lv_obj_t *label, bool *display_valid,
                               int8_t *display_c, bool valid,
                               int8_t temperature_c) {
  if (*display_valid == valid && (!valid || *display_c == temperature_c)) {
    return;
  }

  char text[8];
  if (valid) {
    snprintf(text, sizeof(text), "%d\xC2\xB0" "C", temperature_c);
    *display_c = temperature_c;
  } else {
    snprintf(text, sizeof(text), "--\xC2\xB0" "C");
  }
  lv_label_set_text(label, text);
  apply_temperature_style(label, valid, temperature_c);
  *display_valid = valid;
}

static void update_temperatures(display_home_t *home,
                                const display_state_t *state) {
  bool motor_valid = temperature_from_state(
      state, DISPLAY_STATE_MOTOR_TEMP, state->motor_temp_c);
  update_temperature(home->motor_temp, &home->motor_temp_valid,
                     &home->motor_temp_display_c, motor_valid,
                     state->motor_temp_c);

  bool controller_valid = temperature_from_state(
      state, DISPLAY_STATE_CONTROLLER_TEMP, state->controller_temp_c);
  update_temperature(home->controller_temp, &home->controller_temp_valid,
                     &home->controller_temp_display_c, controller_valid,
                     state->controller_temp_c);
}

static void update_gear(display_home_t *home, const display_state_t *state) {
  uint8_t gear;
  bool valid = gear_from_state(state, &gear);
  if (home->gear_valid == valid && (!valid || home->gear_display == gear)) {
    return;
  }

  for (uint8_t i = 0; i < DISPLAY_HOME_GEAR_COUNT; ++i) {
    set_gear_active(home->gear[i], valid && i < gear);
  }
  home->gear_valid = valid;
  home->gear_display = valid ? gear : 0;
}

void display_home_create(display_home_t *home) {
  if (home == NULL) {
    return;
  }

  *home = (display_home_t){0};
  home->screen = lv_screen_active();
  lv_obj_remove_flag(home->screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(home->screen, lv_color_hex(COLOR_BACKGROUND),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(home->screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(home->screen, 0, LV_PART_MAIN);

  lv_obj_t *battery = lv_obj_create(home->screen);
  make_transparent(battery);
  lv_obj_set_size(battery,
                  DISPLAY_HOME_BATTERY_WIDTH +
                      DISPLAY_HOME_BATTERY_TERMINAL_WIDTH + 4,
                  DISPLAY_HOME_BATTERY_HEIGHT);
  lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -28, 28);

  lv_obj_t *battery_body = lv_obj_create(battery);
  lv_obj_remove_flag(battery_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(battery_body, DISPLAY_HOME_BATTERY_WIDTH,
                  DISPLAY_HOME_BATTERY_HEIGHT);
  lv_obj_set_style_bg_opa(battery_body, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(battery_body, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(battery_body, lv_color_hex(COLOR_TEXT),
                                LV_PART_MAIN);
  lv_obj_set_style_border_opa(battery_body, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_radius(battery_body, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_all(battery_body, 0, LV_PART_MAIN);
  lv_obj_align(battery_body, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t *battery_terminal = lv_obj_create(battery);
  lv_obj_remove_flag(battery_terminal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(battery_terminal, DISPLAY_HOME_BATTERY_TERMINAL_WIDTH,
                  DISPLAY_HOME_BATTERY_TERMINAL_HEIGHT);
  lv_obj_set_style_bg_color(battery_terminal, lv_color_hex(COLOR_TEXT),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(battery_terminal, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_width(battery_terminal, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(battery_terminal, 3, LV_PART_MAIN);
  lv_obj_align(battery_terminal, LV_ALIGN_RIGHT_MID, 0, 0);

  home->battery_voltage = lv_label_create(battery_body);
  lv_label_set_text(home->battery_voltage, "--.- V");
  lv_obj_set_style_text_color(home->battery_voltage, lv_color_hex(COLOR_TEXT),
                              LV_PART_MAIN);
  lv_obj_align(home->battery_voltage, LV_ALIGN_CENTER, 0, 0);

  home->support_mode = lv_label_create(home->screen);
  lv_label_set_text(home->support_mode, "--");
  lv_obj_set_style_text_color(home->support_mode,
                              lv_color_hex(COLOR_GEAR_ACTIVE), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(home->support_mode, 2, LV_PART_MAIN);
  lv_obj_align(home->support_mode, LV_ALIGN_TOP_MID, 0, 158);

  lv_obj_t *speed_group = lv_obj_create(home->screen);
  make_transparent(speed_group);
  lv_obj_set_size(speed_group, 360, 132);
  lv_obj_align(speed_group, LV_ALIGN_TOP_MID, 0, 196);

  home->speed = lv_label_create(speed_group);
  lv_label_set_text(home->speed, "--");
  lv_obj_set_style_text_color(home->speed, lv_color_hex(COLOR_TEXT),
                              LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_48
  lv_obj_set_style_text_font(home->speed, &lv_font_montserrat_48, LV_PART_MAIN);
#endif
  lv_obj_set_style_transform_scale(home->speed, DISPLAY_HOME_SPEED_SCALE,
                                   LV_PART_MAIN);
  lv_obj_align(home->speed, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *speed_unit = lv_label_create(speed_group);
  lv_label_set_text(speed_unit, "km/h");
  lv_obj_set_style_text_color(speed_unit, lv_color_hex(COLOR_MUTED),
                              LV_PART_MAIN);
  lv_obj_align(speed_unit, LV_ALIGN_TOP_MID, 0, 88);

  lv_obj_t *gear_group = lv_obj_create(home->screen);
  make_transparent(gear_group);
  lv_obj_set_size(gear_group,
                  DISPLAY_HOME_GEAR_COUNT * DISPLAY_HOME_GEAR_WIDTH +
                      (DISPLAY_HOME_GEAR_COUNT - 1) * DISPLAY_HOME_GEAR_GAP,
                  DISPLAY_HOME_GEAR_HEIGHT);
  lv_obj_set_flex_flow(gear_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(gear_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(gear_group, DISPLAY_HOME_GEAR_GAP, LV_PART_MAIN);
  lv_obj_align(gear_group, LV_ALIGN_TOP_MID, 0, 350);

  for (uint8_t i = 0; i < DISPLAY_HOME_GEAR_COUNT; ++i) {
    home->gear[i] = lv_obj_create(gear_group);
    lv_obj_remove_flag(home->gear[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(home->gear[i], DISPLAY_HOME_GEAR_WIDTH,
                    DISPLAY_HOME_GEAR_HEIGHT);
    lv_obj_set_style_bg_opa(home->gear[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(home->gear[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(home->gear[i], 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home->gear[i], 0, LV_PART_MAIN);
    set_gear_active(home->gear[i], false);
  }

  lv_obj_t *motor_temp_label = lv_label_create(home->screen);
  lv_label_set_text(motor_temp_label, "MOTOR");
  lv_obj_set_style_text_color(motor_temp_label, lv_color_hex(COLOR_MUTED),
                              LV_PART_MAIN);
  lv_obj_align(motor_temp_label, LV_ALIGN_TOP_MID, -120, 386);

  home->motor_temp = lv_label_create(home->screen);
  lv_label_set_text(home->motor_temp, "--\xC2\xB0" "C");
  lv_obj_set_style_text_color(home->motor_temp, lv_color_hex(COLOR_MUTED),
                              LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_48
  lv_obj_set_style_text_font(home->motor_temp, &lv_font_montserrat_48,
                             LV_PART_MAIN);
#endif
  lv_obj_align(home->motor_temp, LV_ALIGN_TOP_MID, -120, 404);

  lv_obj_t *controller_temp_label = lv_label_create(home->screen);
  lv_label_set_text(controller_temp_label, "CONTROLLER");
  lv_obj_set_style_text_color(controller_temp_label,
                              lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
  lv_obj_align(controller_temp_label, LV_ALIGN_TOP_MID, 120, 386);

  home->controller_temp = lv_label_create(home->screen);
  lv_label_set_text(home->controller_temp, "--\xC2\xB0" "C");
  lv_obj_set_style_text_color(home->controller_temp, lv_color_hex(COLOR_MUTED),
                              LV_PART_MAIN);
#if LV_FONT_MONTSERRAT_48
  lv_obj_set_style_text_font(home->controller_temp, &lv_font_montserrat_48,
                             LV_PART_MAIN);
#endif
  lv_obj_align(home->controller_temp, LV_ALIGN_TOP_MID, 120, 404);
}

void display_home_update(display_home_t *home, const display_state_t *state) {
  if (home == NULL || state == NULL || home->screen == NULL) {
    return;
  }

  update_support_mode(home, state);
  update_speed(home, state);
  update_battery_voltage(home, state);
  update_temperatures(home, state);
  update_gear(home, state);
}
