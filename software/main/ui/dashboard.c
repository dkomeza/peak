#include "ui/dashboard.h"

#include "ui/style.h"
#include <stdio.h>

#define PEAK_DASHBOARD_SEGMENT_COUNT 5
#define PEAK_DASHBOARD_ARC_MAX_KMH 50
#define PEAK_DASHBOARD_ROOT_GAP 8
#define PEAK_DASHBOARD_HERO_HEIGHT 286
#define PEAK_DASHBOARD_ARC_SIZE 286
#define PEAK_DASHBOARD_WIDE_CARD_HEIGHT 116
#define PEAK_DASHBOARD_SMALL_CARD_HEIGHT 74
#define PEAK_DASHBOARD_SPEED_SCALE 384
#define PEAK_DASHBOARD_FONT_SMALL LV_FONT_DEFAULT
#define PEAK_DASHBOARD_FONT_MEDIUM LV_FONT_DEFAULT
#define PEAK_DASHBOARD_RETURN_ON_ERROR(expr)                                      \
  do {                                                                            \
    esp_err_t err_rc = (expr);                                                    \
    if (err_rc != ESP_OK) {                                                       \
      return err_rc;                                                              \
    }                                                                             \
  } while (0)
#define PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(obj)                                \
  do {                                                                            \
    if ((obj) == NULL) {                                                          \
      return ESP_ERR_NO_MEM;                                                      \
    }                                                                             \
  } while (0)

typedef struct {
  lv_obj_t *screen;
  lv_obj_t *status_label;
  lv_obj_t *battery_label;
  lv_obj_t *mode_label;
  lv_obj_t *speed_label;
  lv_obj_t *power_label;
  lv_obj_t *range_label;
  lv_obj_t *thermal_label;
  lv_obj_t *distance_label;
  lv_obj_t *time_label;
  lv_obj_t *average_label;
  lv_obj_t *arc;
  lv_obj_t *segments[PEAK_DASHBOARD_SEGMENT_COUNT];
} peak_dashboard_view_t;

static peak_dashboard_view_t s_view;

static uint16_t clamp_u16(uint16_t value, uint16_t max) {
  return value > max ? max : value;
}

static bool view_ready(void) {
  if (s_view.screen == NULL || s_view.status_label == NULL ||
      s_view.battery_label == NULL || s_view.mode_label == NULL ||
      s_view.speed_label == NULL || s_view.power_label == NULL ||
      s_view.range_label == NULL || s_view.thermal_label == NULL ||
      s_view.distance_label == NULL || s_view.time_label == NULL ||
      s_view.average_label == NULL || s_view.arc == NULL) {
    return false;
  }

  for (int i = 0; i < PEAK_DASHBOARD_SEGMENT_COUNT; i++) {
    if (s_view.segments[i] == NULL) {
      return false;
    }
  }

  return true;
}

static esp_err_t create_label(lv_obj_t *parent, const char *text,
                              lv_color_t color, const lv_font_t *font,
                              lv_obj_t **label_out) {
  lv_obj_t *label = lv_label_create(parent);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(label);
  lv_label_set_text(label, text);
  peak_ui_style_label(label, color, font);
  if (label_out != NULL) {
    *label_out = label;
  }
  return ESP_OK;
}

static esp_err_t create_card(lv_obj_t *parent, int32_t width, int32_t height,
                             lv_obj_t **card_out) {
  lv_obj_t *card = lv_obj_create(parent);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(card);
  peak_ui_style_card(card);
  lv_obj_set_size(card, width, height);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  *card_out = card;
  return ESP_OK;
}

static esp_err_t create_metric_card(lv_obj_t *parent, const char *caption,
                                    lv_obj_t **value_out) {
  lv_obj_t *card = NULL;

  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_card(parent, 0, PEAK_DASHBOARD_SMALL_CARD_HEIGHT, &card));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, 6, LV_PART_MAIN);

  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      card, caption, peak_ui_color_muted(), PEAK_DASHBOARD_FONT_SMALL,
      NULL));
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      card, "--", peak_ui_color_text(), PEAK_DASHBOARD_FONT_MEDIUM, value_out));
  return ESP_OK;
}

static esp_err_t create_status_row(lv_obj_t *screen) {
  lv_obj_t *row = lv_obj_create(screen);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(row);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 42);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *status = lv_obj_create(row);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(status);
  peak_ui_style_pill(status);
  lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status, 112, 36);
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      status, "BT ACTIVE", peak_ui_color_text(), PEAK_DASHBOARD_FONT_SMALL,
      &s_view.status_label));
  lv_obj_center(s_view.status_label);

  lv_obj_t *battery = lv_obj_create(row);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(battery);
  peak_ui_style_pill(battery);
  lv_obj_clear_flag(battery, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(battery, 100, 36);
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      battery, "84% BAT", peak_ui_color_text(), PEAK_DASHBOARD_FONT_SMALL,
      &s_view.battery_label));
  lv_obj_center(s_view.battery_label);

  return ESP_OK;
}

static esp_err_t create_hero(lv_obj_t *screen) {
  lv_obj_t *hero = lv_obj_create(screen);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(hero);
  lv_obj_remove_style_all(hero);
  lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hero, LV_PCT(100), PEAK_DASHBOARD_HERO_HEIGHT);
  lv_obj_set_style_pad_top(hero, 0, LV_PART_MAIN);

  s_view.arc = lv_arc_create(hero);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(s_view.arc);
  lv_obj_set_size(s_view.arc, PEAK_DASHBOARD_ARC_SIZE, PEAK_DASHBOARD_ARC_SIZE);
  lv_obj_align(s_view.arc, LV_ALIGN_TOP_MID, 0, 0);
  lv_arc_set_range(s_view.arc, 0, PEAK_DASHBOARD_ARC_MAX_KMH);
  lv_arc_set_value(s_view.arc, 0);
  lv_arc_set_bg_angles(s_view.arc, 135, 45);
  lv_obj_set_style_arc_width(s_view.arc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_view.arc, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_view.arc, lv_color_hex(0x1B2738),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_view.arc, peak_ui_color_accent(),
                             LV_PART_INDICATOR);
  lv_obj_remove_style(s_view.arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(s_view.arc, LV_OBJ_FLAG_CLICKABLE);

  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      hero, "ECO - TORQUE", peak_ui_color_accent(), PEAK_DASHBOARD_FONT_SMALL,
      &s_view.mode_label));
  lv_obj_align(s_view.mode_label, LV_ALIGN_TOP_MID, 0, 78);

  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      hero, "0", peak_ui_color_text(), PEAK_DASHBOARD_FONT_MEDIUM,
      &s_view.speed_label));
  lv_obj_set_style_transform_scale(s_view.speed_label,
                                   PEAK_DASHBOARD_SPEED_SCALE, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_x(s_view.speed_label, 0, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(s_view.speed_label, 0, LV_PART_MAIN);
  lv_obj_align(s_view.speed_label, LV_ALIGN_TOP_MID, -28, 112);

  lv_obj_t *unit_label = NULL;
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      hero, "km/h", peak_ui_color_muted(), PEAK_DASHBOARD_FONT_SMALL,
      &unit_label));
  lv_obj_align_to(unit_label, s_view.speed_label, LV_ALIGN_OUT_RIGHT_MID, 10,
                  2);

  lv_obj_t *power = lv_obj_create(hero);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(power);
  peak_ui_style_pill(power);
  lv_obj_clear_flag(power, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(power, 176, 48);
  lv_obj_align(power, LV_ALIGN_TOP_MID, 0, 212);
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      power, "0 WATTS", peak_ui_color_text(), PEAK_DASHBOARD_FONT_MEDIUM,
      &s_view.power_label));
  lv_obj_center(s_view.power_label);

  return ESP_OK;
}

static esp_err_t create_segments(lv_obj_t *screen) {
  lv_obj_t *row = lv_obj_create(screen);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(row);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 18);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(row, 12, LV_PART_MAIN);

  for (int i = 0; i < PEAK_DASHBOARD_SEGMENT_COUNT; i++) {
    s_view.segments[i] = lv_obj_create(row);
    PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(s_view.segments[i]);
    lv_obj_remove_style_all(s_view.segments[i]);
    lv_obj_set_flex_grow(s_view.segments[i], 1);
    lv_obj_set_height(s_view.segments[i], 7);
    lv_obj_set_style_radius(s_view.segments[i], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.segments[i], LV_OPA_COVER, LV_PART_MAIN);
  }

  return ESP_OK;
}

static esp_err_t create_info_cards(lv_obj_t *screen) {
  lv_obj_t *wide_row = lv_obj_create(screen);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(wide_row);
  lv_obj_remove_style_all(wide_row);
  lv_obj_set_size(wide_row, LV_PCT(100), PEAK_DASHBOARD_WIDE_CARD_HEIGHT);
  lv_obj_set_flex_flow(wide_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(wide_row, 12, LV_PART_MAIN);

  lv_obj_t *range = NULL;
  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_card(wide_row, 0, PEAK_DASHBOARD_WIDE_CARD_HEIGHT, &range));
  lv_obj_set_flex_grow(range, 1);
  lv_obj_set_flex_flow(range, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(range, 18, LV_PART_MAIN);
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      range, "EST. RANGE", peak_ui_color_muted(), PEAK_DASHBOARD_FONT_SMALL,
      NULL));
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      range, "-- km", peak_ui_color_text(), PEAK_DASHBOARD_FONT_MEDIUM,
      &s_view.range_label));

  lv_obj_t *thermal = NULL;
  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_card(wide_row, 0, PEAK_DASHBOARD_WIDE_CARD_HEIGHT, &thermal));
  lv_obj_set_flex_grow(thermal, 1);
  lv_obj_set_flex_flow(thermal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(thermal, 32, LV_PART_MAIN);
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      thermal, "THERMALS", peak_ui_color_warm(), PEAK_DASHBOARD_FONT_SMALL,
      NULL));
  PEAK_DASHBOARD_RETURN_ON_ERROR(create_label(
      thermal, "M: --  C: --", peak_ui_color_text(), PEAK_DASHBOARD_FONT_SMALL,
      &s_view.thermal_label));

  lv_obj_t *small_row = lv_obj_create(screen);
  PEAK_DASHBOARD_RETURN_NO_MEM_IF_NULL(small_row);
  lv_obj_remove_style_all(small_row);
  lv_obj_set_size(small_row, LV_PCT(100), PEAK_DASHBOARD_SMALL_CARD_HEIGHT);
  lv_obj_set_flex_flow(small_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(small_row, 8, LV_PART_MAIN);

  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_metric_card(small_row, "DIST KM", &s_view.distance_label));
  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_metric_card(small_row, "TIME", &s_view.time_label));
  PEAK_DASHBOARD_RETURN_ON_ERROR(
      create_metric_card(small_row, "AVG KM/H", &s_view.average_label));

  return ESP_OK;
}

esp_err_t peak_dashboard_create(lv_obj_t *parent) {
  if (parent == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_view = (peak_dashboard_view_t){0};
  s_view.screen = parent;

  peak_ui_style_screen(parent);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(parent, PEAK_DASHBOARD_ROOT_GAP, LV_PART_MAIN);

  esp_err_t err = create_status_row(parent);
  if (err == ESP_OK) {
    err = create_hero(parent);
  }
  if (err == ESP_OK) {
    err = create_segments(parent);
  }
  if (err == ESP_OK) {
    err = create_info_cards(parent);
  }
  if (err != ESP_OK) {
    lv_obj_clean(parent);
    s_view = (peak_dashboard_view_t){0};
    return err;
  }

  return ESP_OK;
}

void peak_dashboard_update(const peak_dashboard_data_t *data) {
  if (data == NULL || !view_ready()) {
    return;
  }

  char buffer[32];
  const char *mode_label = data->mode_label != NULL ? data->mode_label : "--";

  lv_label_set_text(s_view.status_label,
                    data->connected ? "BT ACTIVE" : "BT OFFLINE");

  snprintf(buffer, sizeof(buffer), "%u%% BAT",
           (unsigned)clamp_u16(data->battery_percent, 100));
  lv_label_set_text(s_view.battery_label, buffer);

  lv_label_set_text(s_view.mode_label, mode_label);

  snprintf(buffer, sizeof(buffer), "%u", (unsigned)data->speed_kmh);
  lv_label_set_text(s_view.speed_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u WATTS", (unsigned)data->power_watts);
  lv_label_set_text(s_view.power_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u km",
           (unsigned)data->estimated_range_km);
  lv_label_set_text(s_view.range_label, buffer);

  snprintf(buffer, sizeof(buffer), "M: %d  C: %d", data->motor_temp_c,
           data->controller_temp_c);
  lv_label_set_text(s_view.thermal_label, buffer);

  snprintf(buffer, sizeof(buffer), "%.1f", (double)data->trip_distance_km);
  lv_label_set_text(s_view.distance_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u:%02u",
           (unsigned)(data->ride_time_minutes / 60),
           (unsigned)(data->ride_time_minutes % 60));
  lv_label_set_text(s_view.time_label, buffer);

  snprintf(buffer, sizeof(buffer), "%.1f", (double)data->average_speed_kmh);
  lv_label_set_text(s_view.average_label, buffer);

  lv_arc_set_value(s_view.arc,
                   clamp_u16(data->speed_kmh, PEAK_DASHBOARD_ARC_MAX_KMH));

  for (int i = 0; i < PEAK_DASHBOARD_SEGMENT_COUNT; i++) {
    lv_color_t color = i < data->assist_segments ? peak_ui_color_accent()
                                                  : lv_color_hex(0x14201E);
    lv_obj_set_style_bg_color(s_view.segments[i], color, LV_PART_MAIN);
  }
}
