#ifndef LOOM_TYPES_H
#define LOOM_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LOOM_PIXEL_FORMAT_RGB888 = 0,
  LOOM_PIXEL_FORMAT_RGB565 = 1,
} loom_pixel_format_t;

typedef enum {
  LOOM_BITMAP_FORMAT_RGB888 = 0,
  LOOM_BITMAP_FORMAT_RGBA8888 = 1,
  LOOM_BITMAP_FORMAT_A8 = 2,
} loom_bitmap_format_t;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} loom_color_t;

typedef struct {
  int x;
  int y;
} loom_point_t;

typedef struct {
  int x;
  int y;
  int w;
  int h;
} loom_rect_t;

typedef struct {
  uint16_t width;
  uint16_t height;
  loom_bitmap_format_t format;
  uint16_t stride;
  const void *pixels;
} loom_bitmap_t;

typedef struct {
  uint16_t width;
  loom_color_t color;
} loom_stroke_t;

typedef struct {
  uint16_t width;
  uint16_t height;
  loom_pixel_format_t format;
  uint16_t tile_height;
  uint8_t buffer_count;
  size_t command_capacity;
  esp_lcd_panel_handle_t panel;
} loom_display_config_t;

static inline loom_color_t loom_rgb(uint8_t r, uint8_t g, uint8_t b) {
  loom_color_t color = {r, g, b, 255};
  return color;
}

static inline loom_color_t loom_rgba(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t a) {
  loom_color_t color = {r, g, b, a};
  return color;
}

static inline loom_rect_t loom_rect(int x, int y, int w, int h) {
  loom_rect_t rect = {x, y, w, h};
  return rect;
}

#ifdef __cplusplus
}
#endif

#endif
