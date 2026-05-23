#include "loom_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"

static int64_t loom_min_i64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t loom_max_i64(int64_t a, int64_t b) { return a > b ? a : b; }

static int loom_i64_to_int_saturate(int64_t value) {
  if (value > INT_MAX) {
    return INT_MAX;
  }
  if (value < INT_MIN) {
    return INT_MIN;
  }
  return (int)value;
}

static int loom_extent_i64_to_int_saturate(int64_t extent) {
  if (extent <= 0) {
    return 0;
  }
  if (extent > INT_MAX) {
    return INT_MAX;
  }
  return (int)extent;
}

static int64_t loom_rect_right_i64(loom_rect_t rect) {
  return (int64_t)rect.x + (int64_t)rect.w;
}

static int64_t loom_rect_bottom_i64(loom_rect_t rect) {
  return (int64_t)rect.y + (int64_t)rect.h;
}

static loom_rect_t loom_rect_from_edges_i64(int64_t left, int64_t top,
                                            int64_t right, int64_t bottom) {
  if (right <= left || bottom <= top) {
    return loom_rect(loom_i64_to_int_saturate(left),
                     loom_i64_to_int_saturate(top), 0, 0);
  }

  int64_t width = right - left;
  int64_t height = bottom - top;
  int x = loom_i64_to_int_saturate(left);
  int y = loom_i64_to_int_saturate(top);

  if (width > INT_MAX) {
    if (left < 0 && right > 0) {
      x = -(INT_MAX / 2);
    } else if (left < INT_MIN) {
      x = loom_i64_to_int_saturate(right - INT_MAX);
    }
  }

  if (height > INT_MAX) {
    if (top < 0 && bottom > 0) {
      y = -(INT_MAX / 2);
    } else if (top < INT_MIN) {
      y = loom_i64_to_int_saturate(bottom - INT_MAX);
    }
  }

  return loom_rect(x, y, loom_extent_i64_to_int_saturate(width),
                   loom_extent_i64_to_int_saturate(height));
}

static bool loom_rect_is_valid(loom_rect_t rect) {
  return rect.w > 0 && rect.h > 0;
}

static bool loom_stroke_is_valid(const loom_stroke_t *stroke) {
  return stroke != NULL && stroke->width > 0;
}

static loom_rect_t loom_rect_normalized_from_points(loom_point_t p0,
                                                    loom_point_t p1) {
  int64_t left = loom_min_i64(p0.x, p1.x);
  int64_t top = loom_min_i64(p0.y, p1.y);
  int64_t right = loom_max_i64(p0.x, p1.x) + 1;
  int64_t bottom = loom_max_i64(p0.y, p1.y) + 1;
  return loom_rect_from_edges_i64(left, top, right, bottom);
}

static loom_rect_t loom_rect_outset(loom_rect_t rect, uint16_t amount) {
  if (loom_rect_is_empty(rect)) {
    return rect;
  }

  int64_t outset = amount;
  return loom_rect_from_edges_i64((int64_t)rect.x - outset,
                                  (int64_t)rect.y - outset,
                                  loom_rect_right_i64(rect) + outset,
                                  loom_rect_bottom_i64(rect) + outset);
}

static const loom_glyph_t *loom_find_glyph(const loom_font_t *font,
                                           uint32_t codepoint) {
  if (font == NULL || font->glyphs == NULL) {
    return NULL;
  }

  for (uint16_t i = 0; i < font->glyph_count; ++i) {
    if (font->glyphs[i].codepoint == codepoint) {
      return &font->glyphs[i];
    }
  }

  return NULL;
}

static loom_rect_t loom_text_bounds(const loom_font_t *font, const char *text,
                                    int x, int y) {
  int64_t pen_x = x;
  int64_t min_x = INT64_MAX;
  int64_t min_y = INT64_MAX;
  int64_t max_x = INT64_MIN;
  int64_t max_y = INT64_MIN;
  bool has_glyph_bounds = false;
  bool missing_glyph = false;

  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p) {
    const loom_glyph_t *glyph = loom_find_glyph(font, *p);
    if (glyph == NULL && font->fallback_codepoint != 0) {
      glyph = loom_find_glyph(font, font->fallback_codepoint);
    }
    if (glyph == NULL) {
      missing_glyph = true;
      pen_x += font->line_height > 0 ? font->line_height / 2 : 8;
      continue;
    }

    int64_t glyph_left = pen_x + glyph->bearing_x;
    int64_t glyph_top = (int64_t)y + font->baseline - glyph->bearing_y;
    int64_t glyph_right = glyph_left + glyph->width;
    int64_t glyph_bottom = glyph_top + glyph->height;

    min_x = loom_min_i64(min_x, glyph_left);
    min_y = loom_min_i64(min_y, glyph_top);
    max_x = loom_max_i64(max_x, glyph_right);
    max_y = loom_max_i64(max_y, glyph_bottom);
    has_glyph_bounds = true;

    pen_x += glyph->advance_x;
  }

  int line_height = font->line_height > 0 ? font->line_height : 16;
  if (!has_glyph_bounds || missing_glyph) {
    int64_t width = pen_x > x ? pen_x - x : line_height;
    return loom_rect_from_edges_i64(x, y, (int64_t)x + width,
                                    (int64_t)y + line_height);
  }

  if (max_y - min_y < line_height) {
    min_y = y;
    max_y = (int64_t)y + line_height;
  }

  return loom_rect_from_edges_i64(min_x, min_y, max_x, max_y);
}

static esp_err_t loom_bitmap_min_stride(const loom_bitmap_t *bitmap,
                                        size_t *min_stride) {
  size_t bytes_per_pixel = 0;

  if (bitmap == NULL || min_stride == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  switch (bitmap->format) {
  case LOOM_BITMAP_FORMAT_RGB888:
    bytes_per_pixel = 3;
    break;
  case LOOM_BITMAP_FORMAT_RGBA8888:
    bytes_per_pixel = 4;
    break;
  case LOOM_BITMAP_FORMAT_A8:
    bytes_per_pixel = 1;
    break;
  default:
    return ESP_ERR_INVALID_ARG;
  }

  if (bitmap->width != 0 && bytes_per_pixel > SIZE_MAX / bitmap->width) {
    return ESP_ERR_INVALID_ARG;
  }

  *min_stride = (size_t)bitmap->width * bytes_per_pixel;
  return ESP_OK;
}

static char *loom_strdup_internal(const char *text) {
  size_t len = strlen(text);
  if (len == SIZE_MAX) {
    return NULL;
  }

  char *copy = heap_caps_malloc(len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, text, len + 1);
  return copy;
}

static esp_err_t loom_require_frame(const loom_t *loom) {
  if (loom == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static void loom_union_dirty(loom_t *loom, loom_rect_t rect) {
  if (!loom->dirty_valid) {
    loom->dirty = rect;
    loom->dirty_valid = true;
    return;
  }

  loom->dirty = loom_rect_union(loom->dirty, rect);
}

loom_rect_t loom_screen_rect(const loom_t *loom) {
  if (loom == NULL) {
    return loom_rect(0, 0, 0, 0);
  }
  return loom_rect(0, 0, loom->config.width, loom->config.height);
}

bool loom_rect_is_empty(loom_rect_t rect) {
  return rect.w <= 0 || rect.h <= 0;
}

bool loom_rect_intersect(loom_rect_t a, loom_rect_t b, loom_rect_t *out) {
  int64_t left = loom_max_i64(a.x, b.x);
  int64_t top = loom_max_i64(a.y, b.y);
  int64_t right = loom_min_i64(loom_rect_right_i64(a), loom_rect_right_i64(b));
  int64_t bottom =
      loom_min_i64(loom_rect_bottom_i64(a), loom_rect_bottom_i64(b));
  loom_rect_t result = loom_rect_from_edges_i64(left, top, right, bottom);

  if (out != NULL) {
    *out = result;
  }

  return !loom_rect_is_empty(result);
}

loom_rect_t loom_rect_union(loom_rect_t a, loom_rect_t b) {
  if (loom_rect_is_empty(a)) {
    return b;
  }
  if (loom_rect_is_empty(b)) {
    return a;
  }

  int64_t left = loom_min_i64(a.x, b.x);
  int64_t top = loom_min_i64(a.y, b.y);
  int64_t right = loom_max_i64(loom_rect_right_i64(a), loom_rect_right_i64(b));
  int64_t bottom =
      loom_max_i64(loom_rect_bottom_i64(a), loom_rect_bottom_i64(b));
  return loom_rect_from_edges_i64(left, top, right, bottom);
}

loom_rect_t loom_current_clip(const loom_t *loom) {
  if (loom == NULL || loom->clip_depth == 0) {
    return loom_rect(0, 0, 0, 0);
  }
  return loom->clip_stack[loom->clip_depth - 1];
}

loom_rect_t loom_clip_to_screen(const loom_t *loom, loom_rect_t rect) {
  loom_rect_t clipped;
  loom_rect_intersect(rect, loom_screen_rect(loom), &clipped);
  return clipped;
}

void loom_release_frame_texts(loom_t *loom) {
  if (loom == NULL || loom->commands == NULL) {
    return;
  }

  for (size_t i = 0; i < loom->command_count; ++i) {
    loom_command_t *command = &loom->commands[i];
    if (command->type == LOOM_CMD_TEXT && command->data.text.text != NULL) {
      heap_caps_free(command->data.text.text);
      command->data.text.text = NULL;
    }
  }
}

esp_err_t loom_command_append(loom_t *loom, const loom_command_t *command) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (command == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (loom->sticky_error != ESP_OK) {
    return loom->sticky_error;
  }
  if (loom->command_count >= loom->command_capacity) {
    loom->sticky_error = ESP_ERR_NO_MEM;
    return loom->sticky_error;
  }

  loom_command_t stored = *command;
  stored.clip = loom_current_clip(loom);

  loom_rect_t clipped_bounds;
  loom_rect_intersect(stored.bounds, stored.clip, &clipped_bounds);
  clipped_bounds = loom_clip_to_screen(loom, clipped_bounds);

  loom->commands[loom->command_count++] = stored;
  if (!loom_rect_is_empty(clipped_bounds)) {
    loom_union_dirty(loom, clipped_bounds);
  }

  return ESP_OK;
}

esp_err_t loom_invalidate_rect(loom_t *loom, loom_rect_t rect) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_rect_t clipped = loom_clip_to_screen(loom, rect);
  if (loom_rect_is_empty(clipped)) {
    return ESP_OK;
  }

  loom_union_dirty(loom, clipped);
  return ESP_OK;
}

esp_err_t loom_push_clip(loom_t *loom, loom_rect_t rect) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (loom->clip_depth >= LOOM_MAX_CLIP_DEPTH) {
    return ESP_ERR_NO_MEM;
  }

  loom_rect_t clipped;
  loom_rect_intersect(rect, loom_current_clip(loom), &clipped);
  loom->clip_stack[loom->clip_depth++] = clipped;
  return ESP_OK;
}

esp_err_t loom_pop_clip(loom_t *loom) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (loom->clip_depth <= 1) {
    return ESP_ERR_INVALID_STATE;
  }

  --loom->clip_depth;
  return ESP_OK;
}

esp_err_t loom_clear(loom_t *loom, loom_color_t color) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }

  loom_command_t command = {
      .type = LOOM_CMD_CLEAR,
      .bounds = loom_screen_rect(loom),
      .data.shape = {.rect = loom_screen_rect(loom), .color = color},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_fill_rect(loom_t *loom, loom_rect_t rect, loom_color_t color) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_command_t command = {
      .type = LOOM_CMD_FILL_RECT,
      .bounds = rect,
      .data.shape = {.rect = rect, .color = color},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_stroke_rect(loom_t *loom, loom_rect_t rect,
                           const loom_stroke_t *stroke) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect) || !loom_stroke_is_valid(stroke)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_command_t command = {
      .type = LOOM_CMD_STROKE_RECT,
      .bounds = loom_rect_outset(rect, stroke->width),
      .data.shape = {.rect = rect, .stroke = *stroke},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_fill_round_rect(loom_t *loom, loom_rect_t rect, uint16_t radius,
                               loom_color_t color) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_command_t command = {
      .type = LOOM_CMD_FILL_ROUND_RECT,
      .bounds = rect,
      .data.shape = {.rect = rect, .color = color, .radius = radius},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_stroke_round_rect(loom_t *loom, loom_rect_t rect,
                                 uint16_t radius,
                                 const loom_stroke_t *stroke) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(rect) || !loom_stroke_is_valid(stroke)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_command_t command = {
      .type = LOOM_CMD_STROKE_ROUND_RECT,
      .bounds = loom_rect_outset(rect, stroke->width),
      .data.shape = {.rect = rect, .radius = radius, .stroke = *stroke},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_draw_line(loom_t *loom, loom_point_t p0, loom_point_t p1,
                         const loom_stroke_t *stroke) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_stroke_is_valid(stroke)) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_rect_t bounds = loom_rect_outset(loom_rect_normalized_from_points(p0, p1),
                                        stroke->width);
  loom_command_t command = {
      .type = LOOM_CMD_LINE,
      .bounds = bounds,
      .data.line = {.p0 = p0, .p1 = p1, .stroke = *stroke},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_draw_arc(loom_t *loom, loom_point_t center, uint16_t radius,
                        int16_t start_degrees, int16_t sweep_degrees,
                        const loom_stroke_t *stroke) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (radius == 0 || sweep_degrees == 0 || !loom_stroke_is_valid(stroke)) {
    return ESP_ERR_INVALID_ARG;
  }

  int64_t extent = (int64_t)radius + stroke->width;
  loom_rect_t bounds = loom_rect_from_edges_i64(
      (int64_t)center.x - extent, (int64_t)center.y - extent,
      (int64_t)center.x + extent + 1, (int64_t)center.y + extent + 1);
  loom_command_t command = {
      .type = LOOM_CMD_ARC,
      .bounds = bounds,
      .data.arc = {.center = center,
                   .radius = radius,
                   .start_degrees = start_degrees,
                   .sweep_degrees = sweep_degrees,
                   .stroke = *stroke},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_draw_bitmap(loom_t *loom, loom_rect_t dst,
                           const loom_bitmap_t *bitmap, loom_color_t tint) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (!loom_rect_is_valid(dst) || bitmap == NULL || bitmap->pixels == NULL ||
      bitmap->width == 0 || bitmap->height == 0 || bitmap->stride == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t min_stride = 0;
  if (loom_bitmap_min_stride(bitmap, &min_stride) != ESP_OK ||
      bitmap->stride < min_stride) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_command_t command = {
      .type = LOOM_CMD_BITMAP,
      .bounds = dst,
      .data.bitmap = {.dst = dst, .bitmap = bitmap, .tint = tint},
  };
  return loom_command_append(loom, &command);
}

esp_err_t loom_draw_text(loom_t *loom, const loom_font_t *font,
                         const char *text, int x, int y,
                         const loom_text_style_t *style) {
  esp_err_t ret = loom_require_frame(loom);
  if (ret != ESP_OK) {
    return ret;
  }
  if (font == NULL || text == NULL || text[0] == '\0' || style == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_rect_t bounds = loom_text_bounds(font, text, x, y);
  if (loom_rect_is_empty(bounds)) {
    return ESP_ERR_INVALID_ARG;
  }

  char *text_copy = loom_strdup_internal(text);
  if (text_copy == NULL) {
    return ESP_ERR_NO_MEM;
  }

  loom_command_t command = {
      .type = LOOM_CMD_TEXT,
      .bounds = bounds,
      .data.text =
          {.font = font, .text = text_copy, .x = x, .y = y, .style = *style},
  };
  ret = loom_command_append(loom, &command);
  if (ret != ESP_OK) {
    heap_caps_free(text_copy);
  }
  return ret;
}
