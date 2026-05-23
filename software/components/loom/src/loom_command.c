#include "loom_internal.h"

#include <limits.h>
static int loom_min_int(int a, int b) { return a < b ? a : b; }
static int loom_max_int(int a, int b) { return a > b ? a : b; }

static bool loom_rect_is_valid(loom_rect_t rect) {
  return rect.w > 0 && rect.h > 0;
}

static bool loom_stroke_is_valid(const loom_stroke_t *stroke) {
  return stroke != NULL && stroke->width > 0;
}

static loom_rect_t loom_rect_normalized_from_points(loom_point_t p0,
                                                    loom_point_t p1) {
  int left = loom_min_int(p0.x, p1.x);
  int top = loom_min_int(p0.y, p1.y);
  int right = loom_max_int(p0.x, p1.x);
  int bottom = loom_max_int(p0.y, p1.y);
  return loom_rect(left, top, right - left + 1, bottom - top + 1);
}

static loom_rect_t loom_rect_outset(loom_rect_t rect, uint16_t amount) {
  if (loom_rect_is_empty(rect)) {
    return rect;
  }

  int outset = amount;
  return loom_rect(rect.x - outset, rect.y - outset, rect.w + (outset * 2),
                   rect.h + (outset * 2));
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
  int pen_x = x;
  int min_x = INT_MAX;
  int min_y = INT_MAX;
  int max_x = INT_MIN;
  int max_y = INT_MIN;
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

    int glyph_left = pen_x + glyph->bearing_x;
    int glyph_top = y + font->baseline - glyph->bearing_y;
    int glyph_right = glyph_left + glyph->width;
    int glyph_bottom = glyph_top + glyph->height;

    min_x = loom_min_int(min_x, glyph_left);
    min_y = loom_min_int(min_y, glyph_top);
    max_x = loom_max_int(max_x, glyph_right);
    max_y = loom_max_int(max_y, glyph_bottom);
    has_glyph_bounds = true;

    pen_x += glyph->advance_x;
  }

  int line_height = font->line_height > 0 ? font->line_height : 16;
  if (!has_glyph_bounds || missing_glyph) {
    int width = pen_x > x ? pen_x - x : line_height;
    return loom_rect(x, y, width, line_height);
  }

  if (max_y - min_y < line_height) {
    min_y = y;
    max_y = y + line_height;
  }

  return loom_rect(min_x, min_y, max_x - min_x, max_y - min_y);
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
  int left = loom_max_int(a.x, b.x);
  int top = loom_max_int(a.y, b.y);
  int right = loom_min_int(a.x + a.w, b.x + b.w);
  int bottom = loom_min_int(a.y + a.h, b.y + b.h);
  loom_rect_t result = loom_rect(left, top, right - left, bottom - top);

  if (out != NULL) {
    *out = loom_rect_is_empty(result) ? loom_rect(left, top, 0, 0) : result;
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

  int left = loom_min_int(a.x, b.x);
  int top = loom_min_int(a.y, b.y);
  int right = loom_max_int(a.x + a.w, b.x + b.w);
  int bottom = loom_max_int(a.y + a.h, b.y + b.h);
  return loom_rect(left, top, right - left, bottom - top);
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

  int extent = radius + stroke->width;
  loom_rect_t bounds = loom_rect(center.x - extent, center.y - extent,
                                 extent * 2 + 1, extent * 2 + 1);
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

  loom_command_t command = {
      .type = LOOM_CMD_TEXT,
      .bounds = bounds,
      .data.text = {.font = font, .text = text, .x = x, .y = y, .style = *style},
  };
  return loom_command_append(loom, &command);
}
