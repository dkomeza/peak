#ifndef LOOM_FONT_H
#define LOOM_FONT_H

#include <stdint.h>
#include "loom/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  LOOM_FONT_ATLAS_A8 = 0,
  LOOM_FONT_ATLAS_SDF_A8 = 1,
} loom_font_atlas_format_t;

typedef struct {
  uint32_t codepoint;
  uint16_t atlas_x;
  uint16_t atlas_y;
  uint16_t width;
  uint16_t height;
  int16_t bearing_x;
  int16_t bearing_y;
  int16_t advance_x;
} loom_glyph_t;

typedef struct loom_font {
  loom_font_atlas_format_t format;
  uint16_t atlas_width;
  uint16_t atlas_height;
  uint16_t atlas_stride;
  const uint8_t *atlas;
  const loom_glyph_t *glyphs;
  uint16_t glyph_count;
  uint32_t fallback_codepoint;
  int16_t line_height;
  int16_t baseline;
  uint8_t sdf_px_range;
} loom_font_t;

typedef struct {
  loom_color_t color;
  uint8_t opacity;
  uint16_t size_px;
} loom_text_style_t;

#ifdef __cplusplus
}
#endif

#endif
