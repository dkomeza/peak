#include "loom_internal.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int loom_abs_int(int value) { return value < 0 ? -value : value; }

static int loom_min_int(int a, int b) { return a < b ? a : b; }

static int loom_max_int(int a, int b) { return a > b ? a : b; }

static int64_t loom_abs_i64(int64_t value) {
  return value < 0 ? -value : value;
}

static int64_t loom_rect_right64(loom_rect_t rect) {
  return (int64_t)rect.x + (int64_t)rect.w;
}

static int64_t loom_rect_bottom64(loom_rect_t rect) {
  return (int64_t)rect.y + (int64_t)rect.h;
}

static loom_rect_t loom_rect_inset(loom_rect_t rect, int amount) {
  int64_t left = (int64_t)rect.x + amount;
  int64_t top = (int64_t)rect.y + amount;
  int64_t right = loom_rect_right64(rect) - amount;
  int64_t bottom = loom_rect_bottom64(rect) - amount;
  if (right <= left || bottom <= top) {
    return loom_rect(rect.x, rect.y, 0, 0);
  }
  if (left < INT_MIN || top < INT_MIN || right > INT_MAX || bottom > INT_MAX) {
    return loom_rect(rect.x, rect.y, 0, 0);
  }
  return loom_rect((int)left, (int)top, (int)(right - left),
                   (int)(bottom - top));
}

static bool loom_intersect_command(const loom_command_t *command,
                                   loom_rect_t tile_rect, loom_rect_t *out) {
  loom_rect_t clipped;
  if (!loom_rect_intersect(command->bounds, tile_rect, &clipped)) {
    return false;
  }
  return loom_rect_intersect(clipped, command->clip, out);
}

static uint8_t loom_mul_u8(uint8_t a, uint8_t b) {
  return (uint8_t)(((uint16_t)a * b + 127u) / 255u);
}

static uint8_t loom_blend_channel(uint8_t src, uint8_t dst, uint8_t alpha) {
  uint16_t inv = (uint16_t)(255u - alpha);
  return (uint8_t)(((uint16_t)src * alpha + (uint16_t)dst * inv + 127u) / 255u);
}

static uint8_t *loom_tile_pixel(uint8_t *tile, const loom_t *loom,
                                loom_rect_t tile_rect, int x, int y) {
  size_t local_y = (size_t)(y - tile_rect.y);
  return tile + local_y * loom->tile_stride +
         (size_t)x * LOOM_RGB888_BYTES_PER_PIXEL;
}

static void loom_write_pixel(uint8_t *tile, const loom_t *loom,
                             loom_rect_t tile_rect, int x, int y,
                             loom_color_t color) {
  uint8_t *dst = loom_tile_pixel(tile, loom, tile_rect, x, y);
  if (color.a == 255) {
    dst[0] = color.r;
    dst[1] = color.g;
    dst[2] = color.b;
    return;
  }

  if (color.a == 0) {
    return;
  }

  dst[0] = loom_blend_channel(color.r, dst[0], color.a);
  dst[1] = loom_blend_channel(color.g, dst[1], color.a);
  dst[2] = loom_blend_channel(color.b, dst[2], color.a);
}

static void loom_fill_span(uint8_t *tile, const loom_t *loom,
                           loom_rect_t tile_rect, int y, int x0, int x1,
                           loom_color_t color) {
  if (x1 <= x0 || color.a == 0) {
    return;
  }

  uint8_t *dst = loom_tile_pixel(tile, loom, tile_rect, x0, y);
  if (color.a == 255) {
    for (int x = x0; x < x1; ++x) {
      dst[0] = color.r;
      dst[1] = color.g;
      dst[2] = color.b;
      dst += LOOM_RGB888_BYTES_PER_PIXEL;
    }
    return;
  }

  for (int x = x0; x < x1; ++x) {
    dst[0] = loom_blend_channel(color.r, dst[0], color.a);
    dst[1] = loom_blend_channel(color.g, dst[1], color.a);
    dst[2] = loom_blend_channel(color.b, dst[2], color.a);
    dst += LOOM_RGB888_BYTES_PER_PIXEL;
  }
}

static void loom_fill_rect_clipped(uint8_t *tile, const loom_t *loom,
                                   loom_rect_t tile_rect, loom_rect_t rect,
                                   loom_color_t color) {
  for (int y = rect.y; y < rect.y + rect.h; ++y) {
    loom_fill_span(tile, loom, tile_rect, y, rect.x, rect.x + rect.w, color);
  }
}

static bool loom_point_in_round_rect(int x, int y, loom_rect_t rect,
                                     int radius) {
  if (x < rect.x || y < rect.y || x >= rect.x + rect.w ||
      y >= rect.y + rect.h) {
    return false;
  }
  radius = loom_min_int(radius, loom_min_int(rect.w, rect.h) / 2);
  if (radius <= 0) {
    return true;
  }
  if (x >= rect.x + radius && x < rect.x + rect.w - radius) {
    return true;
  }
  if (y >= rect.y + radius && y < rect.y + rect.h - radius) {
    return true;
  }

  int cx = x < rect.x + radius ? rect.x + radius - 1 : rect.x + rect.w - radius;
  int cy = y < rect.y + radius ? rect.y + radius - 1 : rect.y + rect.h - radius;
  int64_t dx = (int64_t)x - cx;
  int64_t dy = (int64_t)y - cy;
  return dx * dx + dy * dy <= (int64_t)radius * radius;
}

static void loom_raster_fill_round_rect(uint8_t *tile, const loom_t *loom,
                                        loom_rect_t tile_rect, loom_rect_t rect,
                                        uint16_t radius, loom_rect_t clip,
                                        loom_color_t color) {
  loom_rect_t visible;
  if (!loom_rect_intersect(rect, clip, &visible)) {
    return;
  }

  int r = loom_min_int(radius, loom_min_int(rect.w, rect.h) / 2);
  for (int y = visible.y; y < visible.y + visible.h; ++y) {
    int run_start = -1;
    for (int x = visible.x; x < visible.x + visible.w; ++x) {
      if (loom_point_in_round_rect(x, y, rect, r)) {
        if (run_start < 0) {
          run_start = x;
        }
      } else if (run_start >= 0) {
        loom_fill_span(tile, loom, tile_rect, y, run_start, x, color);
        run_start = -1;
      }
    }
    if (run_start >= 0) {
      loom_fill_span(tile, loom, tile_rect, y, run_start, visible.x + visible.w,
                     color);
    }
  }
}

static void loom_raster_stroke_rect(uint8_t *tile, const loom_t *loom,
                                    loom_rect_t tile_rect, loom_rect_t rect,
                                    uint16_t width, loom_rect_t clip,
                                    loom_color_t color) {
  if (width == 0) {
    return;
  }

  int outer_amount = (int)width / 2;
  int inner_amount = (int)width - outer_amount;
  loom_rect_t outer = loom_rect_inset(rect, -outer_amount);
  loom_rect_t inner = loom_rect_inset(rect, inner_amount);
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      bool in_outer = x >= outer.x && y >= outer.y && x < outer.x + outer.w &&
                      y < outer.y + outer.h;
      bool in_inner = !loom_rect_is_empty(inner) && x >= inner.x &&
                      y >= inner.y && x < inner.x + inner.w &&
                      y < inner.y + inner.h;
      if (in_outer && !in_inner) {
        loom_write_pixel(tile, loom, tile_rect, x, y, color);
      }
    }
  }
}

static void loom_raster_stroke_round_rect(uint8_t *tile, const loom_t *loom,
                                          loom_rect_t tile_rect,
                                          loom_rect_t rect, uint16_t radius,
                                          uint16_t width, loom_rect_t clip,
                                          loom_color_t color) {
  if (width == 0) {
    return;
  }

  int outer_amount = (int)width / 2;
  int inner_amount = (int)width - outer_amount;
  loom_rect_t outer = loom_rect_inset(rect, -outer_amount);
  int outer_radius = loom_min_int((int)radius + outer_amount,
                                  loom_min_int(outer.w, outer.h) / 2);
  loom_rect_t inner = loom_rect_inset(rect, inner_amount);
  int inner_radius = (int)radius - inner_amount;
  if (inner_radius < 0) {
    inner_radius = 0;
  }

  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      bool outer_contains = loom_point_in_round_rect(x, y, outer, outer_radius);
      bool inner_contains = !loom_rect_is_empty(inner) &&
                            loom_point_in_round_rect(x, y, inner, inner_radius);
      if (outer_contains && !inner_contains) {
        loom_write_pixel(tile, loom, tile_rect, x, y, color);
      }
    }
  }
}

static void loom_draw_brush(uint8_t *tile, const loom_t *loom,
                            loom_rect_t tile_rect, int x, int y, uint16_t width,
                            loom_rect_t clip, loom_color_t color) {
  int half = (int)width / 2;
  int start = -half;
  int end = (int)width - half;
  for (int by = start; by < end; ++by) {
    int py = y + by;
    if (py < clip.y || py >= clip.y + clip.h) {
      continue;
    }
    for (int bx = start; bx < end; ++bx) {
      int px = x + bx;
      if (px >= clip.x && px < clip.x + clip.w) {
        loom_write_pixel(tile, loom, tile_rect, px, py, color);
      }
    }
  }
}

static bool loom_clip_line_to_rect(loom_point_t p0, loom_point_t p1,
                                   loom_rect_t clip, uint16_t brush_width,
                                   int64_t *out_x0, int64_t *out_y0,
                                   int64_t *out_x1, int64_t *out_y1) {
  if (loom_rect_is_empty(clip) || brush_width == 0 || out_x0 == NULL ||
      out_y0 == NULL || out_x1 == NULL || out_y1 == NULL) {
    return false;
  }

  double x0 = p0.x;
  double y0 = p0.y;
  double dx = (double)p1.x - (double)p0.x;
  double dy = (double)p1.y - (double)p0.y;
  double t0 = 0.0;
  double t1 = 1.0;
  double half = (double)(brush_width / 2u);
  double left = (double)clip.x - half;
  double top = (double)clip.y - half;
  double right = (double)clip.x + (double)clip.w - 1.0 + half;
  double bottom = (double)clip.y + (double)clip.h - 1.0 + half;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {x0 - left, right - x0, y0 - top, bottom - y0};

  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) {
        return false;
      }
      continue;
    }

    double t = q[i] / p[i];
    if (p[i] < 0.0) {
      if (t > t1) {
        return false;
      }
      if (t > t0) {
        t0 = t;
      }
    } else {
      if (t < t0) {
        return false;
      }
      if (t < t1) {
        t1 = t;
      }
    }
  }

  *out_x0 = (int64_t)llround(x0 + t0 * dx);
  *out_y0 = (int64_t)llround(y0 + t0 * dy);
  *out_x1 = (int64_t)llround(x0 + t1 * dx);
  *out_y1 = (int64_t)llround(y0 + t1 * dy);
  return *out_x0 >= INT_MIN && *out_x0 <= INT_MAX && *out_y0 >= INT_MIN &&
         *out_y0 <= INT_MAX && *out_x1 >= INT_MIN && *out_x1 <= INT_MAX &&
         *out_y1 >= INT_MIN && *out_y1 <= INT_MAX;
}

static void loom_raster_draw_line(uint8_t *tile, const loom_t *loom,
                                  loom_rect_t tile_rect, loom_point_t p0,
                                  loom_point_t p1, loom_stroke_t stroke,
                                  loom_rect_t clip) {
  int64_t x0 = 0;
  int64_t y0 = 0;
  int64_t x1 = 0;
  int64_t y1 = 0;
  if (!loom_clip_line_to_rect(p0, p1, clip, stroke.width, &x0, &y0, &x1, &y1)) {
    return;
  }

  int64_t dx = loom_abs_i64(x1 - x0);
  int64_t sx = x0 < x1 ? 1 : -1;
  int64_t dy = -loom_abs_i64(y1 - y0);
  int64_t sy = y0 < y1 ? 1 : -1;
  int64_t err = dx + dy;

  for (;;) {
    loom_draw_brush(tile, loom, tile_rect, (int)x0, (int)y0, stroke.width, clip,
                    stroke.color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int64_t old_err = err;
    if (old_err >= dy - old_err) {
      err += dy;
      x0 += sx;
    }
    if (old_err <= dx - old_err) {
      err += dx;
      y0 += sy;
    }
  }
}

static void loom_raster_draw_arc(uint8_t *tile, const loom_t *loom,
                                 loom_rect_t tile_rect, loom_point_t center,
                                 uint16_t radius, int16_t start_degrees,
                                 int16_t sweep_degrees, loom_stroke_t stroke,
                                 loom_rect_t clip) {
  int steps = loom_abs_int(sweep_degrees);
  steps = loom_max_int(steps, (int)radius / 2);
  steps = loom_min_int(steps, 720);
  if (steps <= 0) {
    return;
  }

  double start = (double)start_degrees * M_PI / 180.0;
  double sweep = (double)sweep_degrees * M_PI / 180.0;
  loom_point_t prev = {
      center.x + (int)lround(cos(start) * radius),
      center.y + (int)lround(sin(start) * radius),
  };

  for (int i = 1; i <= steps; ++i) {
    double t = start + sweep * (double)i / (double)steps;
    loom_point_t next = {
        center.x + (int)lround(cos(t) * radius),
        center.y + (int)lround(sin(t) * radius),
    };
    loom_raster_draw_line(tile, loom, tile_rect, prev, next, stroke, clip);
    prev = next;
  }
}

static void loom_raster_draw_bitmap(uint8_t *tile, const loom_t *loom,
                                    loom_rect_t tile_rect,
                                    const loom_bitmap_cmd_t *cmd,
                                    loom_rect_t clip) {
  const loom_bitmap_t *bitmap = cmd->bitmap;
  if (bitmap == NULL || bitmap->pixels == NULL || cmd->dst.w <= 0 ||
      cmd->dst.h <= 0) {
    return;
  }

  const uint8_t *pixels = (const uint8_t *)bitmap->pixels;
  for (int y = clip.y; y < clip.y + clip.h; ++y) {
    int sy = (int)(((int64_t)(y - cmd->dst.y) * bitmap->height) / cmd->dst.h);
    if (sy < 0 || sy >= bitmap->height) {
      continue;
    }
    for (int x = clip.x; x < clip.x + clip.w; ++x) {
      int sx = (int)(((int64_t)(x - cmd->dst.x) * bitmap->width) / cmd->dst.w);
      if (sx < 0 || sx >= bitmap->width) {
        continue;
      }

      loom_color_t color = {0, 0, 0, 0};
      const uint8_t *src = pixels + (size_t)sy * bitmap->stride;
      switch (bitmap->format) {
      case LOOM_BITMAP_FORMAT_RGB888:
        src += (size_t)sx * 3u;
        color.r = loom_mul_u8(src[0], cmd->tint.r);
        color.g = loom_mul_u8(src[1], cmd->tint.g);
        color.b = loom_mul_u8(src[2], cmd->tint.b);
        color.a = cmd->tint.a;
        break;
      case LOOM_BITMAP_FORMAT_RGBA8888:
        src += (size_t)sx * 4u;
        color.r = loom_mul_u8(src[0], cmd->tint.r);
        color.g = loom_mul_u8(src[1], cmd->tint.g);
        color.b = loom_mul_u8(src[2], cmd->tint.b);
        color.a = loom_mul_u8(src[3], cmd->tint.a);
        break;
      case LOOM_BITMAP_FORMAT_A8:
        src += sx;
        color = cmd->tint;
        color.a = loom_mul_u8(src[0], cmd->tint.a);
        break;
      default:
        return;
      }
      loom_write_pixel(tile, loom, tile_rect, x, y, color);
    }
  }
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

static const loom_glyph_t *loom_resolve_glyph(const loom_font_t *font,
                                              uint8_t byte) {
  const loom_glyph_t *glyph = loom_find_glyph(font, byte);
  if (glyph == NULL && font->fallback_codepoint != 0) {
    glyph = loom_find_glyph(font, font->fallback_codepoint);
  }
  return glyph;
}

static bool loom_glyph_atlas_valid(const loom_font_t *font,
                                   const loom_glyph_t *glyph) {
  if (font == NULL || glyph == NULL || font->atlas == NULL ||
      font->atlas_stride == 0) {
    return false;
  }
  if (glyph->width == 0 || glyph->height == 0) {
    return true;
  }
  if ((uint32_t)glyph->atlas_x + glyph->width > font->atlas_width ||
      (uint32_t)glyph->atlas_y + glyph->height > font->atlas_height) {
    return false;
  }
  return font->atlas_stride >= (uint32_t)glyph->atlas_x + glyph->width;
}

static void loom_raster_draw_text(uint8_t *tile, const loom_t *loom,
                                  loom_rect_t tile_rect,
                                  const loom_text_cmd_t *cmd,
                                  loom_rect_t clip) {
  const loom_font_t *font = cmd->font;
  if (font == NULL || cmd->text == NULL || font->atlas == NULL) {
    return;
  }

  int pen_x = cmd->x;
  loom_color_t color = cmd->style.color;
  color.a = loom_mul_u8(color.a, cmd->style.opacity);

  for (const unsigned char *p = (const unsigned char *)cmd->text; *p != '\0';
       ++p) {
    const loom_glyph_t *glyph = NULL;
    if (*p < 0x80) {
      glyph = loom_resolve_glyph(font, *p);
    } else {
      glyph = font->fallback_codepoint != 0
                  ? loom_find_glyph(font, font->fallback_codepoint)
                  : NULL;
      while ((p[1] & 0xc0) == 0x80) {
        ++p;
      }
    }

    if (glyph == NULL) {
      pen_x += font->line_height > 0 ? font->line_height / 2 : 8;
      continue;
    }

    if (!loom_glyph_atlas_valid(font, glyph)) {
      pen_x += glyph->advance_x;
      continue;
    }

    loom_rect_t glyph_rect = loom_rect(
        pen_x + glyph->bearing_x, cmd->y + font->baseline - glyph->bearing_y,
        glyph->width, glyph->height);
    loom_rect_t visible;
    if (loom_rect_intersect(glyph_rect, clip, &visible)) {
      for (int y = visible.y; y < visible.y + visible.h; ++y) {
        int gy = y - glyph_rect.y;
        const uint8_t *row =
            font->atlas + (size_t)(glyph->atlas_y + gy) * font->atlas_stride +
            glyph->atlas_x;
        for (int x = visible.x; x < visible.x + visible.w; ++x) {
          int gx = x - glyph_rect.x;
          loom_color_t glyph_color = color;
          glyph_color.a = loom_mul_u8(row[gx], color.a);
          loom_write_pixel(tile, loom, tile_rect, x, y, glyph_color);
        }
      }
    }

    pen_x += glyph->advance_x;
  }
}

esp_err_t loom_render_tile(loom_t *loom, uint8_t *tile, loom_rect_t tile_rect) {
  if (loom == NULL || tile == NULL || loom_rect_is_empty(tile_rect) ||
      tile_rect.x < 0 || tile_rect.y < 0 ||
      loom_rect_right64(tile_rect) > loom->config.width ||
      loom_rect_bottom64(tile_rect) > loom->config.height ||
      tile_rect.h > loom->config.tile_height) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t row_bytes = (size_t)tile_rect.w * LOOM_RGB888_BYTES_PER_PIXEL;
  for (int y = 0; y < tile_rect.h; ++y) {
    memset(tile + (size_t)y * loom->tile_stride +
               (size_t)tile_rect.x * LOOM_RGB888_BYTES_PER_PIXEL,
           0, row_bytes);
  }

  for (size_t i = 0; i < loom->command_count; ++i) {
    const loom_command_t *command = &loom->commands[i];
    loom_rect_t visible;
    if (!loom_intersect_command(command, tile_rect, &visible)) {
      continue;
    }

    switch (command->type) {
    case LOOM_CMD_CLEAR:
      loom_fill_rect_clipped(tile, loom, tile_rect, visible,
                             command->data.shape.color);
      break;
    case LOOM_CMD_FILL_RECT:
      loom_fill_rect_clipped(tile, loom, tile_rect, visible,
                             command->data.shape.color);
      break;
    case LOOM_CMD_STROKE_RECT:
      loom_raster_stroke_rect(tile, loom, tile_rect, command->data.shape.rect,
                              command->data.shape.stroke.width, visible,
                              command->data.shape.stroke.color);
      break;
    case LOOM_CMD_FILL_ROUND_RECT:
      loom_raster_fill_round_rect(
          tile, loom, tile_rect, command->data.shape.rect,
          command->data.shape.radius, visible, command->data.shape.color);
      break;
    case LOOM_CMD_STROKE_ROUND_RECT:
      loom_raster_stroke_round_rect(
          tile, loom, tile_rect, command->data.shape.rect,
          command->data.shape.radius, command->data.shape.stroke.width, visible,
          command->data.shape.stroke.color);
      break;
    case LOOM_CMD_LINE:
      loom_raster_draw_line(tile, loom, tile_rect, command->data.line.p0,
                            command->data.line.p1, command->data.line.stroke,
                            visible);
      break;
    case LOOM_CMD_ARC:
      loom_raster_draw_arc(
          tile, loom, tile_rect, command->data.arc.center,
          command->data.arc.radius, command->data.arc.start_degrees,
          command->data.arc.sweep_degrees, command->data.arc.stroke, visible);
      break;
    case LOOM_CMD_BITMAP:
      loom_raster_draw_bitmap(tile, loom, tile_rect, &command->data.bitmap,
                              visible);
      break;
    case LOOM_CMD_TEXT:
      loom_raster_draw_text(tile, loom, tile_rect, &command->data.text,
                            visible);
      break;
    default:
      break;
    }
  }

  return ESP_OK;
}
