#ifndef LOOM_INTERNAL_H
#define LOOM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "loom/loom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOOM_DEFAULT_COMMAND_CAPACITY 256
#define LOOM_MAX_CLIP_DEPTH 8
#define LOOM_RGB888_BYTES_PER_PIXEL 3
#define LOOM_TILE_ALIGNMENT 64

typedef enum {
  LOOM_CMD_CLEAR,
  LOOM_CMD_FILL_RECT,
  LOOM_CMD_STROKE_RECT,
  LOOM_CMD_FILL_ROUND_RECT,
  LOOM_CMD_STROKE_ROUND_RECT,
  LOOM_CMD_LINE,
  LOOM_CMD_ARC,
  LOOM_CMD_BITMAP,
  LOOM_CMD_TEXT,
} loom_command_type_t;

typedef struct {
  loom_rect_t dst;
  const loom_bitmap_t *bitmap;
  loom_color_t tint;
} loom_bitmap_cmd_t;

typedef struct {
  const loom_font_t *font;
  char *text;
  int x;
  int y;
  loom_text_style_t style;
} loom_text_cmd_t;

typedef struct {
  loom_command_type_t type;
  loom_rect_t bounds;
  loom_rect_t clip;
  union {
    struct {
      loom_rect_t rect;
      loom_color_t color;
      uint16_t radius;
      loom_stroke_t stroke;
    } shape;
    struct {
      loom_point_t p0;
      loom_point_t p1;
      loom_stroke_t stroke;
    } line;
    struct {
      loom_point_t center;
      uint16_t radius;
      int16_t start_degrees;
      int16_t sweep_degrees;
      loom_stroke_t stroke;
    } arc;
    loom_bitmap_cmd_t bitmap;
    loom_text_cmd_t text;
  } data;
} loom_command_t;

struct loom {
  loom_display_config_t config;
  loom_command_t *commands;
  size_t command_count;
  size_t command_capacity;
  loom_rect_t clip_stack[LOOM_MAX_CLIP_DEPTH];
  size_t clip_depth;
  loom_rect_t dirty;
  bool dirty_valid;
  bool in_frame;
  esp_err_t sticky_error;
  uint8_t *tile_buffers[2];
  uint8_t buffer_count;
  size_t tile_stride;
  size_t tile_bytes;
  SemaphoreHandle_t trans_done_sem;
  bool panel_callbacks_registered;
};

esp_err_t loom_command_append(loom_t *loom, const loom_command_t *command);
void loom_release_frame_texts(loom_t *loom);

loom_rect_t loom_current_clip(const loom_t *loom);
loom_rect_t loom_screen_rect(const loom_t *loom);
bool loom_rect_intersect(loom_rect_t a, loom_rect_t b, loom_rect_t *out);
loom_rect_t loom_rect_union(loom_rect_t a, loom_rect_t b);
bool loom_rect_is_empty(loom_rect_t rect);
loom_rect_t loom_clip_to_screen(const loom_t *loom, loom_rect_t rect);

esp_err_t loom_render_tile(loom_t *loom, uint8_t *tile, loom_rect_t tile_rect);
esp_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                             loom_rect_t tile_rect);

#ifdef __cplusplus
}
#endif

#endif
