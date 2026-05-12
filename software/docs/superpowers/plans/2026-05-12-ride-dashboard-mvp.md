# Ride Dashboard MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a bootable LVGL ride dashboard MVP with mock data on the 480x640 Peak display.

**Architecture:** Keep `main/display` focused on ST7701/MIPI panel bring-up and LVGL runtime. Add `main/ui` for style tokens, dashboard widgets, mock data, and future screen navigation. The dashboard consumes a small display-ready data model so KT and Peak/CycleIQ adapters can feed the same UI later.

**Tech Stack:** ESP-IDF as configured by this repo, ESP32-P4, C, FreeRTOS, esp_lcd ST7701/MIPI DSI, LVGL 9 through the ESP-IDF component manager.

---

## File Structure

- Modify `main/idf_component.yml`: add the LVGL component dependency. LVGL's ESP-IDF documentation shows `lvgl/lvgl^9.*` as the registry dependency form for LVGL 9.
- Modify `main/CMakeLists.txt`: compile display and UI sources, and add `lvgl` to component requirements.
- Modify `main/display/display.h`: expose `esp_err_t display_init(void)`.
- Modify `main/display/display.c`: convert the current experiment into a runtime module that initializes the panel, LVGL display, buffers, callbacks, UI, and LVGL task.
- Create `main/ui/style.h`: declare reusable dashboard colors and style helpers.
- Create `main/ui/style.c`: define LVGL colors and reusable helpers for screens, labels, pills, cards, and bars.
- Create `main/ui/dashboard.h`: define `peak_dashboard_data_t` and dashboard update API.
- Create `main/ui/dashboard.c`: create the ride dashboard widgets and update labels/arcs from data.
- Create `main/ui/ui.h`: declare `esp_err_t ui_init(void)`.
- Create `main/ui/ui.c`: initialize styles, create the dashboard, and refresh it with mock data through an LVGL timer.
- Modify `main/main.c`: include `display/display.h` and call `display_init()` during app startup.

## Task 1: Register LVGL And Source Files

**Files:**
- Modify: `main/idf_component.yml`
- Modify: `main/CMakeLists.txt`
- Create: `main/ui/ui.h`
- Create: `main/ui/ui.c`
- Create: `main/ui/style.h`
- Create: `main/ui/style.c`
- Create: `main/ui/dashboard.h`
- Create: `main/ui/dashboard.c`

- [ ] **Step 1: Add the LVGL dependency**

Update `main/idf_component.yml` to:

```yaml
## IDF Component Manager Manifest File
dependencies:
  idf:
    version: '>=6.0.1'
  espressif/esp_wifi_remote: '*'
  espressif/esp_hosted: '*'
  lvgl/lvgl: "^9.*"
```

- [ ] **Step 2: Add empty UI module headers**

Create `main/ui/ui.h`:

```c
#ifndef PEAK_UI_H
#define PEAK_UI_H

#include "esp_err.h"

esp_err_t ui_init(void);

#endif
```

Create `main/ui/style.h`:

```c
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
```

Create `main/ui/dashboard.h`:

```c
#ifndef PEAK_UI_DASHBOARD_H
#define PEAK_UI_DASHBOARD_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t speed_kmh;
  uint8_t battery_percent;
  uint16_t power_watts;
  uint8_t assist_segments;
  uint8_t estimated_range_km;
  float trip_distance_km;
  uint16_t ride_time_minutes;
  float average_speed_kmh;
  int8_t motor_temp_c;
  int8_t controller_temp_c;
  bool connected;
  const char *mode_label;
} peak_dashboard_data_t;

esp_err_t peak_dashboard_create(lv_obj_t *parent);
void peak_dashboard_update(const peak_dashboard_data_t *data);

#endif
```

- [ ] **Step 3: Add temporary implementations**

Create `main/ui/ui.c`:

```c
#include "ui/ui.h"

#include "esp_err.h"

esp_err_t ui_init(void) { return ESP_OK; }
```

Create `main/ui/style.c`:

```c
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
```

Create `main/ui/dashboard.c`:

```c
#include "ui/dashboard.h"

esp_err_t peak_dashboard_create(lv_obj_t *parent) {
  (void)parent;
  return ESP_OK;
}

void peak_dashboard_update(const peak_dashboard_data_t *data) { (void)data; }
```

- [ ] **Step 4: Register sources in CMake**

Update `main/CMakeLists.txt` to:

```cmake
idf_component_register(SRCS "main.c"
                            "buttons.c"
                            "boot/boot.c"
                            "display/display.c"
                            "ui/ui.c"
                            "ui/style.c"
                            "ui/dashboard.c"
                       PRIV_REQUIRES io vesc connection wireless esc
                                      nvs_flash esp_timer esp_lcd lvgl
                       INCLUDE_DIRS "." "boot" "utils"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE)
```

- [ ] **Step 5: Run the build to expose display runtime errors**

Run:

```bash
idf.py build
```

Expected: the build may fail because `main/display/display.c` was previously not compiled by the main component. Proceed to Task 2 to make the display runtime compile.

## Task 2: Refactor Display Runtime

**Files:**
- Modify: `main/display/display.h`
- Modify: `main/display/display.c`

- [ ] **Step 1: Replace the display public header**

Replace `main/display/display.h` with:

```c
#ifndef PEAK_DISPLAY_H
#define PEAK_DISPLAY_H

#include "esp_err.h"

esp_err_t display_init(void);

#endif
```

- [ ] **Step 2: Add runtime constants and includes**

At the top of `main/display/display.c`, include FreeRTOS and UI headers, replace global panel storage with a static handle, and add constants:

```c
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tick/lv_tick.h"
#include "ui/ui.h"
#include <esp_timer.h>
#include <string.h>

static const char *TAG = "display";

#define DISPLAY_H_RES 480
#define DISPLAY_V_RES 640
#define DISPLAY_BPP 3
#define DISPLAY_BUFFER_LINES 128
#define DISPLAY_LVGL_TASK_STACK 8192
#define DISPLAY_LVGL_TASK_PRIORITY 5
#define DISPLAY_BACKLIGHT_GPIO 10
#define DISPLAY_RESET_GPIO 40

static esp_lcd_panel_handle_t s_dpi_panel;
static lv_display_t *s_lvgl_display;
```

Keep the existing `init_cmds[]` array unchanged.

- [ ] **Step 3: Rename and tighten panel initialization**

Rename `esp_lcd_panel_handle_t init(void)` to:

```c
static esp_err_t display_panel_init(esp_lcd_panel_handle_t *out_panel)
```

Inside that function:

- Keep the current LDO, DSI, DBI, DPI, ST7701, reset, init, and backlight sequence.
- Replace the local `esp_lcd_panel_handle_t dpi_panel;` with `esp_lcd_panel_handle_t panel = NULL;`.
- Write the created panel into `*out_panel`.
- Return `ESP_OK`.

The end of the function should be:

```c
  esp_lcd_panel_handle_t panel = NULL;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(dbi_io, &panel_dev_cfg, &panel));
  ESP_LOGI(TAG, "ST7701 panel created successfully");
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

  gpio_set_direction(DISPLAY_BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(DISPLAY_BACKLIGHT_GPIO, 1);

  *out_panel = panel;
  return ESP_OK;
}
```

- [ ] **Step 4: Make flush and completion callbacks use static state**

Keep `my_get_millis()` as a static function:

```c
static uint32_t display_get_millis(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
```

Update the flush callback:

```c
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area,
                             uint8_t *px_buf) {
  (void)disp;
  esp_lcd_panel_draw_bitmap(s_dpi_panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, px_buf);
}
```

Update transfer completion:

```c
static bool display_notify_flush_ready(esp_lcd_panel_handle_t panel,
                                       esp_lcd_dpi_panel_event_data_t *edata,
                                       void *user_ctx) {
  (void)panel;
  (void)edata;
  lv_display_t *disp = (lv_display_t *)user_ctx;
  lv_display_flush_ready(disp);
  return false;
}
```

- [ ] **Step 5: Start the LVGL task**

Replace the current `lvgl_task()` with:

```c
static void display_lvgl_task(void *arg) {
  (void)arg;

  for (;;) {
    uint32_t delay_ms = lv_timer_handler();
    if (delay_ms < 5) {
      delay_ms = 5;
    }
    if (delay_ms > 50) {
      delay_ms = 50;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}
```

- [ ] **Step 6: Replace `display_init()`**

Replace `void display_init()` with:

```c
esp_err_t display_init(void) {
  ESP_RETURN_ON_ERROR(display_panel_init(&s_dpi_panel), TAG,
                      "panel init failed");

  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)display_get_millis);

  s_lvgl_display = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
  if (s_lvgl_display == NULL) {
    return ESP_ERR_NO_MEM;
  }

  lv_display_set_color_format(s_lvgl_display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_flush_cb(s_lvgl_display, display_flush_cb);

  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_color_trans_done = display_notify_flush_ready,
  };
  ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(
      s_dpi_panel, &cbs, s_lvgl_display));

  size_t draw_buffer_sz =
      DISPLAY_H_RES * DISPLAY_BUFFER_LINES * DISPLAY_BPP;
  void *buf1 = heap_caps_aligned_alloc(64, draw_buffer_sz,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (buf1 == NULL) {
    buf1 = heap_caps_aligned_alloc(64, draw_buffer_sz,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  }
  void *buf2 = heap_caps_aligned_alloc(64, draw_buffer_sz,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (buf2 == NULL) {
    buf2 = heap_caps_aligned_alloc(64, draw_buffer_sz,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  }
  if (buf1 == NULL || buf2 == NULL) {
    return ESP_ERR_NO_MEM;
  }

  lv_display_set_buffers(s_lvgl_display, buf1, buf2, draw_buffer_sz,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  ESP_RETURN_ON_ERROR(ui_init(), TAG, "ui init failed");

  BaseType_t task_created =
      xTaskCreatePinnedToCore(display_lvgl_task, "lvgl", DISPLAY_LVGL_TASK_STACK,
                              NULL, DISPLAY_LVGL_TASK_PRIORITY, NULL, 1);
  return task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
```

If `ESP_RETURN_ON_ERROR` is unavailable from the current includes, add:

```c
#include "esp_check.h"
```

- [ ] **Step 7: Run the build**

Run:

```bash
idf.py build
```

Expected: display runtime compiles. Remaining errors should be limited to LVGL API name mismatches if the fetched LVGL version differs from the headers assumed by the existing experiment. Fix only exact API mismatches surfaced by the compiler.

- [ ] **Step 8: Commit the display runtime slice**

Run:

```bash
git add main/idf_component.yml main/CMakeLists.txt main/display/display.h main/display/display.c main/ui
git commit -m "feat: add LVGL display runtime" -m "Compile the existing ST7701 display experiment as a proper display runtime. Register LVGL through the component manager, add UI module stubs, allocate draw buffers, register flush callbacks, initialize the UI, and start the LVGL task."
```

## Task 3: Implement UI Style Tokens

**Files:**
- Modify: `main/ui/style.c`

- [ ] **Step 1: Replace temporary style helpers**

Replace `main/ui/style.c` with:

```c
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
  lv_obj_set_style_bg_color(obj, peak_ui_color_bg(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 16, LV_PART_MAIN);
}

void peak_ui_style_card(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, peak_ui_color_panel(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x2A3337), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, PEAK_RADIUS_CARD, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 14, LV_PART_MAIN);
}

void peak_ui_style_pill(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, peak_ui_color_panel_alt(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x2A3337), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, PEAK_RADIUS_PILL, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(obj, 14, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(obj, 8, LV_PART_MAIN);
}

void peak_ui_style_label(lv_obj_t *obj, lv_color_t color,
                         const lv_font_t *font) {
  lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(obj, 0, LV_PART_MAIN);
}
```

- [ ] **Step 2: Run the build**

Run:

```bash
idf.py build
```

Expected: PASS.

- [ ] **Step 3: Commit the style slice**

Run:

```bash
git add main/ui/style.c
git commit -m "feat: add dashboard style tokens" -m "Centralize the dark dashboard colors and LVGL object styling helpers. These tokens keep the MVP screen visually consistent and leave room for richer gauge and font work later."
```

## Task 4: Build Dashboard Screen Widgets

**Files:**
- Modify: `main/ui/dashboard.c`

- [ ] **Step 1: Replace dashboard implementation**

Replace `main/ui/dashboard.c` with:

```c
#include "ui/dashboard.h"

#include "esp_check.h"
#include "ui/style.h"
#include <stdio.h>

static const char *TAG = "dashboard";

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
  lv_obj_t *segments[5];
} peak_dashboard_view_t;

static peak_dashboard_view_t s_view;

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              lv_color_t color, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  peak_ui_style_label(label, color, font);
  return label;
}

static lv_obj_t *create_card(lv_obj_t *parent, int32_t width, int32_t height) {
  lv_obj_t *card = lv_obj_create(parent);
  peak_ui_style_card(card);
  lv_obj_set_size(card, width, height);
  return card;
}

static lv_obj_t *create_metric_card(lv_obj_t *parent, const char *caption,
                                    lv_obj_t **value_out) {
  lv_obj_t *card = create_card(parent, 0, 78);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, 8, LV_PART_MAIN);

  create_label(card, caption, peak_ui_color_muted(), &lv_font_montserrat_12);
  *value_out =
      create_label(card, "--", peak_ui_color_text(), &lv_font_montserrat_18);
  return card;
}

static void create_status_row(lv_obj_t *screen) {
  lv_obj_t *row = lv_obj_create(screen);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 42);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *status = lv_obj_create(row);
  peak_ui_style_pill(status);
  lv_obj_set_size(status, 104, 36);
  s_view.status_label = create_label(status, "BT ACTIVE", peak_ui_color_text(),
                                     &lv_font_montserrat_12);
  lv_obj_center(s_view.status_label);

  lv_obj_t *battery = lv_obj_create(row);
  peak_ui_style_pill(battery);
  lv_obj_set_size(battery, 96, 36);
  s_view.battery_label =
      create_label(battery, "84% BAT", peak_ui_color_text(),
                   &lv_font_montserrat_12);
  lv_obj_center(s_view.battery_label);
}

static void create_hero(lv_obj_t *screen) {
  lv_obj_t *hero = lv_obj_create(screen);
  lv_obj_remove_style_all(hero);
  lv_obj_set_size(hero, LV_PCT(100), 300);
  lv_obj_set_style_pad_top(hero, 0, LV_PART_MAIN);

  s_view.arc = lv_arc_create(hero);
  lv_obj_set_size(s_view.arc, 300, 300);
  lv_obj_align(s_view.arc, LV_ALIGN_TOP_MID, 0, 0);
  lv_arc_set_range(s_view.arc, 0, 50);
  lv_arc_set_value(s_view.arc, 4);
  lv_arc_set_bg_angles(s_view.arc, 135, 45);
  lv_obj_set_style_arc_width(s_view.arc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_view.arc, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_view.arc, lv_color_hex(0x1B2738),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_view.arc, peak_ui_color_accent(),
                             LV_PART_INDICATOR);
  lv_obj_remove_style(s_view.arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(s_view.arc, LV_OBJ_FLAG_CLICKABLE);

  s_view.mode_label =
      create_label(hero, "ECO - TORQUE", peak_ui_color_accent(),
                   &lv_font_montserrat_14);
  lv_obj_align(s_view.mode_label, LV_ALIGN_TOP_MID, 0, 78);

  s_view.speed_label =
      create_label(hero, "4", peak_ui_color_text(), &lv_font_montserrat_48);
  lv_obj_align(s_view.speed_label, LV_ALIGN_TOP_MID, -16, 106);

  lv_obj_t *unit_label =
      create_label(hero, "km/h", peak_ui_color_muted(), &lv_font_montserrat_18);
  lv_obj_align_to(unit_label, s_view.speed_label, LV_ALIGN_OUT_RIGHT_MID, 10,
                  -10);

  lv_obj_t *power = lv_obj_create(hero);
  peak_ui_style_pill(power);
  lv_obj_set_size(power, 176, 54);
  lv_obj_align(power, LV_ALIGN_TOP_MID, 0, 214);
  s_view.power_label =
      create_label(power, "67 WATTS", peak_ui_color_text(),
                   &lv_font_montserrat_22);
  lv_obj_center(s_view.power_label);
}

static void create_segments(lv_obj_t *screen) {
  lv_obj_t *row = lv_obj_create(screen);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 18);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(row, 12, LV_PART_MAIN);

  for (int i = 0; i < 5; i++) {
    s_view.segments[i] = lv_obj_create(row);
    lv_obj_remove_style_all(s_view.segments[i]);
    lv_obj_set_flex_grow(s_view.segments[i], 1);
    lv_obj_set_height(s_view.segments[i], 7);
    lv_obj_set_style_radius(s_view.segments[i], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_view.segments[i], LV_OPA_COVER, LV_PART_MAIN);
  }
}

static void create_info_cards(lv_obj_t *screen) {
  lv_obj_t *wide_row = lv_obj_create(screen);
  lv_obj_remove_style_all(wide_row);
  lv_obj_set_size(wide_row, LV_PCT(100), 128);
  lv_obj_set_flex_flow(wide_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(wide_row, 12, LV_PART_MAIN);

  lv_obj_t *range = create_card(wide_row, 0, 128);
  lv_obj_set_flex_grow(range, 1);
  lv_obj_set_flex_flow(range, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(range, 26, LV_PART_MAIN);
  create_label(range, "EST. RANGE", peak_ui_color_muted(),
               &lv_font_montserrat_12);
  s_view.range_label =
      create_label(range, "-- km", peak_ui_color_text(), &lv_font_montserrat_34);

  lv_obj_t *thermal = create_card(wide_row, 0, 128);
  lv_obj_set_flex_grow(thermal, 1);
  lv_obj_set_flex_flow(thermal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(thermal, 40, LV_PART_MAIN);
  create_label(thermal, "THERMALS", peak_ui_color_warm(),
               &lv_font_montserrat_12);
  s_view.thermal_label =
      create_label(thermal, "M: --  C: --", peak_ui_color_text(),
                   &lv_font_montserrat_14);

  lv_obj_t *small_row = lv_obj_create(screen);
  lv_obj_remove_style_all(small_row);
  lv_obj_set_size(small_row, LV_PCT(100), 78);
  lv_obj_set_flex_flow(small_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(small_row, 8, LV_PART_MAIN);

  create_metric_card(small_row, "DIST KM", &s_view.distance_label);
  create_metric_card(small_row, "TIME", &s_view.time_label);
  create_metric_card(small_row, "AVG KM/H", &s_view.average_label);
}

esp_err_t peak_dashboard_create(lv_obj_t *parent) {
  if (parent == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_view = (peak_dashboard_view_t){0};
  s_view.screen = parent;
  peak_ui_style_screen(parent);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(parent, 12, LV_PART_MAIN);

  create_status_row(parent);
  create_hero(parent);
  create_segments(parent);
  create_info_cards(parent);

  return ESP_OK;
}

void peak_dashboard_update(const peak_dashboard_data_t *data) {
  if (data == NULL || s_view.screen == NULL) {
    return;
  }

  char buffer[32];

  lv_label_set_text(s_view.status_label,
                    data->connected ? "BT ACTIVE" : "BT OFFLINE");

  snprintf(buffer, sizeof(buffer), "%u%% BAT", data->battery_percent);
  lv_label_set_text(s_view.battery_label, buffer);

  lv_label_set_text(s_view.mode_label, data->mode_label);

  snprintf(buffer, sizeof(buffer), "%u", data->speed_kmh);
  lv_label_set_text(s_view.speed_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u WATTS", data->power_watts);
  lv_label_set_text(s_view.power_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u km", data->estimated_range_km);
  lv_label_set_text(s_view.range_label, buffer);

  snprintf(buffer, sizeof(buffer), "M: %d  C: %d", data->motor_temp_c,
           data->controller_temp_c);
  lv_label_set_text(s_view.thermal_label, buffer);

  snprintf(buffer, sizeof(buffer), "%.1f", data->trip_distance_km);
  lv_label_set_text(s_view.distance_label, buffer);

  snprintf(buffer, sizeof(buffer), "%u:%02u", data->ride_time_minutes / 60,
           data->ride_time_minutes % 60);
  lv_label_set_text(s_view.time_label, buffer);

  snprintf(buffer, sizeof(buffer), "%.1f", data->average_speed_kmh);
  lv_label_set_text(s_view.average_label, buffer);

  lv_arc_set_value(s_view.arc, data->speed_kmh);

  for (int i = 0; i < 5; i++) {
    lv_color_t color = i < data->assist_segments ? peak_ui_color_accent()
                                                  : lv_color_hex(0x14201E);
    lv_obj_set_style_bg_color(s_view.segments[i], color, LV_PART_MAIN);
  }
}
```

- [ ] **Step 2: Run the build**

Run:

```bash
idf.py build
```

Expected: PASS. If `lv_font_montserrat_48` is not enabled in the generated LVGL config, use the largest enabled built-in Montserrat font reported by the compiler and update only the font references.

- [ ] **Step 3: Commit the dashboard slice**

Run:

```bash
git add main/ui/dashboard.c main/ui/dashboard.h
git commit -m "feat: add ride dashboard screen" -m "Create the MVP LVGL dashboard hierarchy with status, battery, speed, power, segment, range, thermal, distance, time, and average speed widgets. The dashboard updates from a display-ready data model and remains independent of ESC-specific data sources."
```

## Task 5: Add UI Startup And Mock Data Timer

**Files:**
- Modify: `main/ui/ui.c`

- [ ] **Step 1: Replace UI startup implementation**

Replace `main/ui/ui.c` with:

```c
#include "ui/ui.h"

#include "esp_check.h"
#include "ui/dashboard.h"
#include "ui/style.h"

static const char *TAG = "ui";

static peak_dashboard_data_t make_mock_data(uint32_t tick) {
  uint16_t speed = 4 + (tick % 18);
  return (peak_dashboard_data_t){
      .speed_kmh = speed,
      .battery_percent = 84 - (tick % 9),
      .power_watts = 67 + ((tick * 17) % 260),
      .assist_segments = 1 + (tick % 5),
      .estimated_range_km = 85 - (tick % 12),
      .trip_distance_km = 24.8f + ((float)(tick % 20) / 10.0f),
      .ride_time_minutes = 84 + (tick % 40),
      .average_speed_kmh = 22.4f + ((float)(tick % 8) / 10.0f),
      .motor_temp_c = 62 + (tick % 5),
      .controller_temp_c = 80 + (tick % 4),
      .connected = true,
      .mode_label = "ECO - TORQUE",
  };
}

static void mock_timer_cb(lv_timer_t *timer) {
  uint32_t *tick = (uint32_t *)lv_timer_get_user_data(timer);
  peak_dashboard_data_t data = make_mock_data(*tick);
  peak_dashboard_update(&data);
  (*tick)++;
}

esp_err_t ui_init(void) {
  peak_ui_style_init();

  lv_obj_t *screen = lv_obj_create(NULL);
  ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_NO_MEM, TAG,
                      "failed to create dashboard screen");

  ESP_RETURN_ON_ERROR(peak_dashboard_create(screen), TAG,
                      "failed to create dashboard");

  static uint32_t mock_tick = 0;
  peak_dashboard_data_t initial_data = make_mock_data(mock_tick);
  peak_dashboard_update(&initial_data);
  mock_tick++;

  lv_screen_load(screen);

  lv_timer_t *timer = lv_timer_create(mock_timer_cb, 1000, &mock_tick);
  ESP_RETURN_ON_FALSE(timer != NULL, ESP_ERR_NO_MEM, TAG,
                      "failed to create mock data timer");

  return ESP_OK;
}
```

- [ ] **Step 2: Run the build**

Run:

```bash
idf.py build
```

Expected: PASS.

- [ ] **Step 3: Commit the UI startup slice**

Run:

```bash
git add main/ui/ui.c
git commit -m "feat: add dashboard mock data" -m "Initialize the LVGL dashboard screen from the UI layer and refresh it with mock ride data on an LVGL timer. This keeps MVP rendering independent from ESC and sensor integration."
```

## Task 6: Start Display From App Startup

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Include the display API**

Add this include near the other local includes in `main/main.c`:

```c
#include "display/display.h"
```

- [ ] **Step 2: Initialize the display after buttons**

In `peak_app_task`, immediately after `buttons_init();`, add:

```c
  ESP_ERROR_CHECK(display_init());
```

The beginning of `peak_app_task` should become:

```c
static void peak_app_task(void *arg) {
  (void)arg;
  nvs_init();

  buttons_init();
  ESP_ERROR_CHECK(display_init());

  // boot_mode_t mode = boot(mountain_mode_callback);
```

- [ ] **Step 3: Run the build**

Run:

```bash
idf.py build
```

Expected: PASS.

- [ ] **Step 4: Commit the startup slice**

Run:

```bash
git add main/main.c
git commit -m "feat: start ride dashboard UI" -m "Initialize the display runtime during app startup so the LVGL ride dashboard boots with the firmware. The dashboard still uses mock data and does not depend on live ESC wiring."
```

## Task 7: Final Verification

**Files:**
- Verify: all files touched by Tasks 1-6

- [ ] **Step 1: Review final diff**

Run:

```bash
git status --short
git diff --stat HEAD~4..HEAD
```

Expected: only UI/display/dashboard/source registration changes are included, plus dependency lock updates produced by `idf.py build`.

- [ ] **Step 2: Run final build**

Run:

```bash
idf.py build
```

Expected: PASS.

- [ ] **Step 3: Optional hardware smoke test**

If the ESP32-P4 display hardware is connected, run:

```bash
idf.py flash monitor
```

Expected: logs show display initialization succeeds, the backlight turns on, the dashboard renders, and logs do not show flush callback failures, draw buffer allocation failures, or task watchdog resets.

- [ ] **Step 4: Commit dependency lock updates if present**

If `idf.py build` updates `dependencies.lock`, run:

```bash
git add dependencies.lock
git commit -m "chore: lock LVGL dependency" -m "Record the component manager lockfile updates produced by adding LVGL to the firmware build."
```

If `dependencies.lock` is unchanged, skip this commit.

## Self-Review Notes

- Spec coverage: this plan adds LVGL/runtime setup, a separate UI layer, a dashboard data model, mock data updates, the reference-inspired hierarchy, centralized styling, app startup wiring, and build verification.
- Scope control: live ESC, battery, sensor, OTA, custom font, full navigation, and final glow/gauge artwork remain outside this MVP.
- Dependency note: LVGL component manager usage follows the LVGL ESP-IDF documentation for `lvgl/lvgl^9.*`; ESP-IDF component manager documentation confirms project dependencies are declared in `idf_component.yml`.
