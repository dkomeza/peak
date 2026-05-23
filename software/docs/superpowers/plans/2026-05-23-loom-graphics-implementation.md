# Loom Graphics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first working `loom` graphics component for ESP32-P4, with RGB888 tiled immediate-mode rendering, core primitives, atlas-text support, and a display smoke demo.

**Architecture:** `loom` is an ESP-IDF component that records immediate-mode draw commands and replays them into renderer-owned RGB888 partial tiles. The current `main/display` code remains responsible for ST7701/MIPI setup and hands the panel handle to `loom`; `loom_end_frame` clips commands by tile and flushes rendered tiles through the ESP LCD panel backend.

**Tech Stack:** C, ESP-IDF component CMake, `esp_err_t`, `esp_heap_caps`, `esp_lcd_panel_ops`, ESP32-P4 RGB888 buffers, optional future PPA/DMA2D backend hooks.

---

## File Structure

- Create `components/loom/CMakeLists.txt`: registers the component, public includes, and source files.
- Create `components/loom/include/loom/types.h`: format-neutral public geometry, colors, bitmap, stroke, and config types.
- Create `components/loom/include/loom/font.h`: atlas font data structures for A8 and SDF font assets.
- Create `components/loom/include/loom/loom.h`: public renderer lifecycle and drawing API.
- Create `components/loom/src/loom_internal.h`: private renderer, command, tile, and raster declarations.
- Create `components/loom/src/loom.c`: lifecycle, frame state, tile allocation, dirty region handling, and frame rendering.
- Create `components/loom/src/loom_command.c`: command appending, bounds calculation, clip stack, and state validation.
- Create `components/loom/src/loom_raster.c`: CPU raster paths for RGB888 clear, rects, lines, rounded rectangles, bitmaps, and atlas text.
- Create `components/loom/src/loom_backend_esp32p4.c`: ESP LCD panel flush and capability flags; PPA hooks are isolated here for later acceleration.
- Modify `main/CMakeLists.txt`: add `loom` to the app component requirements after the component builds standalone.
- Modify `main/display/display.c`: replace the current direct-pixel demo with a `loom` smoke demo after panel init.

The first implementation intentionally avoids adding a retained UI layer, dynamic font generation, or full PPA primitive acceleration. It creates the boundaries needed for those later without forcing them into the first working milestone.

## Task 0: Prepare Execution Workspace

**Files:**
- Read: repository state only.

- [ ] **Step 1: Check whether the current checkout is already an isolated worktree**

Run:

```bash
GIT_DIR=$(cd "$(git rev-parse --git-dir)" 2>/dev/null && pwd -P)
GIT_COMMON=$(cd "$(git rev-parse --git-common-dir)" 2>/dev/null && pwd -P)
BRANCH=$(git branch --show-current)
SUPERPROJECT=$(git rev-parse --show-superproject-working-tree 2>/dev/null || true)
printf 'GIT_DIR=%s\nGIT_COMMON=%s\nBRANCH=%s\nSUPERPROJECT=%s\n' "$GIT_DIR" "$GIT_COMMON" "$BRANCH" "$SUPERPROJECT"
```

Expected: prints the git paths and current branch. If `GIT_DIR != GIT_COMMON` and `SUPERPROJECT` is empty, continue in the isolated workspace. Otherwise ask whether to create a worktree before implementation.

- [ ] **Step 2: Capture dirty baseline**

Run:

```bash
git status --short
```

Expected: existing user changes remain visible, including current display and build configuration work. Do not revert them.

- [ ] **Step 3: Run baseline build if ESP-IDF is available**

Run:

```bash
idf.py build
```

Expected: either the existing app builds, or the command fails for a pre-existing reason. If `idf.py` is not available, record that implementation verification will be limited until ESP-IDF is sourced.

## Task 1: Add Public Component API

**Files:**
- Create: `components/loom/CMakeLists.txt`
- Create: `components/loom/include/loom/types.h`
- Create: `components/loom/include/loom/font.h`
- Create: `components/loom/include/loom/loom.h`

- [ ] **Step 1: Add component registration**

Create `components/loom/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "src/loom.c"
        "src/loom_command.c"
        "src/loom_raster.c"
        "src/loom_backend_esp32p4.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_lcd heap
)
```

- [ ] **Step 2: Add public types**

Create `components/loom/include/loom/types.h` with:

```c
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
  loom_color_t color = {.r = r, .g = g, .b = b, .a = 255};
  return color;
}

static inline loom_color_t loom_rgba(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t a) {
  loom_color_t color = {.r = r, .g = g, .b = b, .a = a};
  return color;
}

static inline loom_rect_t loom_rect(int x, int y, int w, int h) {
  loom_rect_t rect = {.x = x, .y = y, .w = w, .h = h};
  return rect;
}

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 3: Add font types**

Create `components/loom/include/loom/font.h` with:

```c
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
```

- [ ] **Step 4: Add public drawing API**

Create `components/loom/include/loom/loom.h` with:

```c
#ifndef LOOM_H
#define LOOM_H

#include "esp_err.h"
#include "loom/font.h"
#include "loom/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom loom_t;

esp_err_t loom_create(const loom_display_config_t *config, loom_t **out_loom);
void loom_destroy(loom_t *loom);

esp_err_t loom_begin_frame(loom_t *loom);
esp_err_t loom_end_frame(loom_t *loom);
esp_err_t loom_invalidate_rect(loom_t *loom, loom_rect_t rect);

esp_err_t loom_push_clip(loom_t *loom, loom_rect_t rect);
esp_err_t loom_pop_clip(loom_t *loom);

esp_err_t loom_clear(loom_t *loom, loom_color_t color);
esp_err_t loom_fill_rect(loom_t *loom, loom_rect_t rect, loom_color_t color);
esp_err_t loom_stroke_rect(loom_t *loom, loom_rect_t rect,
                           const loom_stroke_t *stroke);
esp_err_t loom_fill_round_rect(loom_t *loom, loom_rect_t rect, uint16_t radius,
                               loom_color_t color);
esp_err_t loom_stroke_round_rect(loom_t *loom, loom_rect_t rect,
                                 uint16_t radius,
                                 const loom_stroke_t *stroke);
esp_err_t loom_draw_line(loom_t *loom, loom_point_t p0, loom_point_t p1,
                         const loom_stroke_t *stroke);
esp_err_t loom_draw_arc(loom_t *loom, loom_point_t center, uint16_t radius,
                        int16_t start_degrees, int16_t sweep_degrees,
                        const loom_stroke_t *stroke);
esp_err_t loom_draw_bitmap(loom_t *loom, loom_rect_t dst,
                           const loom_bitmap_t *bitmap, loom_color_t tint);
esp_err_t loom_draw_text(loom_t *loom, const loom_font_t *font,
                         const char *text, int x, int y,
                         const loom_text_style_t *style);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 5: Build to verify headers and component metadata**

Run:

```bash
idf.py build
```

Expected: build reaches the link or app compile stage. If it fails because the component is not referenced yet, continue; if it fails due to header syntax, fix before proceeding.

- [ ] **Step 6: Commit**

Run:

```bash
git add components/loom/CMakeLists.txt components/loom/include/loom/types.h components/loom/include/loom/font.h components/loom/include/loom/loom.h
git commit -m "feat: add loom public API" -m "Create the initial loom ESP-IDF component API for display configuration, drawing primitives, bitmap input, and atlas font data. Keep the API format-neutral while defaulting the first backend to RGB888."
```

## Task 2: Add Renderer State And Command Recording

**Files:**
- Create: `components/loom/src/loom_internal.h`
- Create: `components/loom/src/loom.c`
- Create: `components/loom/src/loom_command.c`

- [ ] **Step 1: Add private state and command declarations**

Create `components/loom/src/loom_internal.h` with:

```c
#ifndef LOOM_INTERNAL_H
#define LOOM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "loom/loom.h"

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
  const char *text;
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
};

esp_err_t loom_command_append(loom_t *loom, const loom_command_t *command);
loom_rect_t loom_current_clip(const loom_t *loom);
loom_rect_t loom_screen_rect(const loom_t *loom);
bool loom_rect_intersect(loom_rect_t a, loom_rect_t b, loom_rect_t *out);
loom_rect_t loom_rect_union(loom_rect_t a, loom_rect_t b);
bool loom_rect_is_empty(loom_rect_t rect);
loom_rect_t loom_clip_to_screen(const loom_t *loom, loom_rect_t rect);

esp_err_t loom_render_tile(loom_t *loom, uint8_t *tile, loom_rect_t tile_rect);
esp_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                             loom_rect_t tile_rect);

#endif
```

- [ ] **Step 2: Implement lifecycle and frame rendering shell**

Create `components/loom/src/loom.c` with:

```c
#include "loom_internal.h"
#include <string.h>
#include "esp_heap_caps.h"

static uint16_t default_tile_height(uint16_t height) {
  uint16_t tenth = height / 10;
  return tenth > 0 ? tenth : height;
}

esp_err_t loom_create(const loom_display_config_t *config, loom_t **out_loom) {
  if (!config || !out_loom || !config->panel || config->width == 0 ||
      config->height == 0 || config->format != LOOM_PIXEL_FORMAT_RGB888) {
    return ESP_ERR_INVALID_ARG;
  }

  loom_t *loom = heap_caps_calloc(1, sizeof(*loom), MALLOC_CAP_INTERNAL);
  if (!loom) {
    return ESP_ERR_NO_MEM;
  }

  loom->config = *config;
  if (loom->config.tile_height == 0) {
    loom->config.tile_height = default_tile_height(config->height);
  }
  loom->buffer_count = config->buffer_count >= 2 ? 2 : 1;
  loom->command_capacity = config->command_capacity > 0
                               ? config->command_capacity
                               : LOOM_DEFAULT_COMMAND_CAPACITY;
  loom->tile_stride = (size_t)config->width * LOOM_RGB888_BYTES_PER_PIXEL;
  loom->tile_bytes = loom->tile_stride * loom->config.tile_height;
  loom->commands = heap_caps_calloc(loom->command_capacity,
                                    sizeof(loom_command_t),
                                    MALLOC_CAP_INTERNAL);
  if (!loom->commands) {
    loom_destroy(loom);
    return ESP_ERR_NO_MEM;
  }

  for (uint8_t i = 0; i < loom->buffer_count; ++i) {
    loom->tile_buffers[i] = heap_caps_aligned_alloc(
        LOOM_TILE_ALIGNMENT, loom->tile_bytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!loom->tile_buffers[i]) {
      loom->tile_buffers[i] = heap_caps_aligned_alloc(
          LOOM_TILE_ALIGNMENT, loom->tile_bytes,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    if (!loom->tile_buffers[i]) {
      loom_destroy(loom);
      return ESP_ERR_NO_MEM;
    }
  }

  loom->clip_stack[0] = loom_screen_rect(loom);
  loom->clip_depth = 1;
  *out_loom = loom;
  return ESP_OK;
}

void loom_destroy(loom_t *loom) {
  if (!loom) {
    return;
  }
  for (uint8_t i = 0; i < 2; ++i) {
    heap_caps_free(loom->tile_buffers[i]);
  }
  heap_caps_free(loom->commands);
  heap_caps_free(loom);
}

esp_err_t loom_begin_frame(loom_t *loom) {
  if (!loom || loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }
  loom->command_count = 0;
  loom->clip_stack[0] = loom_screen_rect(loom);
  loom->clip_depth = 1;
  loom->dirty_valid = false;
  loom->sticky_error = ESP_OK;
  loom->in_frame = true;
  return ESP_OK;
}

esp_err_t loom_end_frame(loom_t *loom) {
  if (!loom || !loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = loom->sticky_error;
  if (ret == ESP_OK) {
    loom_rect_t dirty = loom->dirty_valid ? loom->dirty : loom_screen_rect(loom);
    dirty = loom_clip_to_screen(loom, dirty);
    for (int y = dirty.y; y < dirty.y + dirty.h; y += loom->config.tile_height) {
      int tile_h = loom->config.tile_height;
      if (y + tile_h > dirty.y + dirty.h) {
        tile_h = dirty.y + dirty.h - y;
      }
      loom_rect_t tile_rect = {.x = 0, .y = y, .w = loom->config.width, .h = tile_h};
      uint8_t *tile = loom->tile_buffers[(y / loom->config.tile_height) %
                                         loom->buffer_count];
      ret = loom_render_tile(loom, tile, tile_rect);
      if (ret != ESP_OK) {
        break;
      }
      ret = loom_backend_flush(loom, tile, tile_rect);
      if (ret != ESP_OK) {
        break;
      }
    }
  }
  loom->in_frame = false;
  return ret;
}
```

- [ ] **Step 3: Implement command recording and geometry helpers**

Create `components/loom/src/loom_command.c` with functions for frame validation, `loom_clear`, rects, lines, arcs, bitmaps, text, clipping, dirty union, and geometry helpers. Every draw function must call `loom_command_append` and return `ESP_ERR_INVALID_STATE` when called outside a frame.

The key append behavior must be:

```c
esp_err_t loom_command_append(loom_t *loom, const loom_command_t *command) {
  if (!loom || !command || !loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }
  if (loom->sticky_error != ESP_OK) {
    return loom->sticky_error;
  }
  if (loom->command_count >= loom->command_capacity) {
    loom->sticky_error = ESP_ERR_NO_MEM;
    return loom->sticky_error;
  }
  loom->commands[loom->command_count++] = *command;
  if (!loom_rect_is_empty(command->bounds)) {
    loom->dirty = loom->dirty_valid ? loom_rect_union(loom->dirty, command->bounds)
                                    : command->bounds;
    loom->dirty_valid = true;
  }
  return ESP_OK;
}
```

- [ ] **Step 4: Build after command implementation**

Run:

```bash
idf.py build
```

Expected: compile reaches missing raster/backend symbols only if those files are still empty. Resolve syntax and type errors before continuing.

- [ ] **Step 5: Commit**

Run:

```bash
git add components/loom/src/loom_internal.h components/loom/src/loom.c components/loom/src/loom_command.c
git commit -m "feat: record loom draw commands" -m "Add loom renderer state, tile allocation, frame lifecycle, dirty tracking, clipping, and command recording. Rendering is still delegated to the raster and backend modules."
```

## Task 3: Implement RGB888 Rasterizer

**Files:**
- Create: `components/loom/src/loom_raster.c`

- [ ] **Step 1: Add tile clear and pixel helpers**

Create `components/loom/src/loom_raster.c` with local helpers:

```c
#include "loom_internal.h"
#include <stdlib.h>
#include <string.h>

static uint8_t blend_channel(uint8_t dst, uint8_t src, uint8_t alpha) {
  uint16_t inv = 255 - alpha;
  return (uint8_t)(((uint16_t)src * alpha + (uint16_t)dst * inv + 127) / 255);
}

static void put_pixel(uint8_t *tile, const loom_t *loom, loom_rect_t tile_rect,
                      int x, int y, loom_color_t color) {
  if (x < tile_rect.x || y < tile_rect.y || x >= tile_rect.x + tile_rect.w ||
      y >= tile_rect.y + tile_rect.h || color.a == 0) {
    return;
  }
  size_t offset = (size_t)(y - tile_rect.y) * loom->tile_stride +
                  (size_t)x * LOOM_RGB888_BYTES_PER_PIXEL;
  uint8_t *px = tile + offset;
  if (color.a == 255) {
    px[0] = color.r;
    px[1] = color.g;
    px[2] = color.b;
    return;
  }
  px[0] = blend_channel(px[0], color.r, color.a);
  px[1] = blend_channel(px[1], color.g, color.a);
  px[2] = blend_channel(px[2], color.b, color.a);
}
```

- [ ] **Step 2: Implement command dispatch**

Add `loom_render_tile` that clears the tile to black, loops over all commands, intersects command bounds with the tile and command clip, then dispatches to helpers for each command type. `LOOM_CMD_CLEAR` must fill the whole tile with the clear color before later commands draw.

- [ ] **Step 3: Implement solid rectangles and strokes**

Add helpers that clip fill bounds to tile-local visible bounds and write RGB888 spans. `loom_fill_rect` and `loom_stroke_rect` must render deterministically even when partially outside the screen.

- [ ] **Step 4: Implement lines**

Add a clipped Bresenham-style integer line path. For stroke widths greater than one, draw a square brush around each point. The initial line implementation is not antialiased.

- [ ] **Step 5: Implement rounded rectangles and arcs**

Add CPU helpers:

- Filled rounded rectangle: fill center strips and test corner pixels against `dx * dx + dy * dy <= radius * radius`.
- Stroked rounded rectangle: draw four straight edge strokes and four arc segments with the same integer arc helper.
- Arc: approximate with one-degree samples and draw line segments between adjacent points.

- [ ] **Step 6: Implement bitmap and text composition**

Add bitmap support for `LOOM_BITMAP_FORMAT_RGB888`, `LOOM_BITMAP_FORMAT_RGBA8888`, and `LOOM_BITMAP_FORMAT_A8`. Add text support by decoding ASCII/UTF-8 single-byte codepoints first, looking up glyphs in `loom_font_t`, reading A8 coverage from the atlas, and blending the styled foreground color into the tile. For `LOOM_FONT_ATLAS_SDF_A8`, treat the atlas value as coverage in the first milestone; true SDF smoothing is a follow-up behind the same API.

- [ ] **Step 7: Build after rasterizer implementation**

Run:

```bash
idf.py build
```

Expected: unresolved symbol errors only for `loom_backend_flush` if the backend is not implemented yet. Raster source must compile cleanly.

- [ ] **Step 8: Commit**

Run:

```bash
git add components/loom/src/loom_raster.c
git commit -m "feat: render loom primitives to RGB888" -m "Add the initial CPU rasterizer for RGB888 tiles, including clears, rectangles, lines, rounded rectangles, arcs, bitmaps, and atlas text composition. Keep the rasterizer independent of the panel backend."
```

## Task 4: Add ESP32-P4 Panel Backend

**Files:**
- Create: `components/loom/src/loom_backend_esp32p4.c`

- [ ] **Step 1: Implement panel flush**

Create `components/loom/src/loom_backend_esp32p4.c`:

```c
#include "loom_internal.h"
#include "esp_lcd_panel_ops.h"

esp_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                             loom_rect_t tile_rect) {
  if (!loom || !tile || !loom->config.panel || loom_rect_is_empty(tile_rect)) {
    return ESP_ERR_INVALID_ARG;
  }
  return esp_lcd_panel_draw_bitmap(loom->config.panel, tile_rect.x, tile_rect.y,
                                   tile_rect.x + tile_rect.w,
                                   tile_rect.y + tile_rect.h, tile);
}
```

- [ ] **Step 2: Build the standalone component**

Run:

```bash
idf.py build
```

Expected: `components/loom` compiles and links into the app if referenced. Any API mismatch with ESP LCD must be fixed before integration.

- [ ] **Step 3: Commit**

Run:

```bash
git add components/loom/src/loom_backend_esp32p4.c
git commit -m "feat: flush loom tiles to esp lcd" -m "Add the ESP32-P4 backend entry point that flushes rendered RGB888 tiles through the existing esp_lcd panel handle. Keep accelerator-specific hooks isolated for later PPA work."
```

## Task 5: Wire Loom Into Display Smoke Demo

**Files:**
- Modify: `main/CMakeLists.txt`
- Modify: `main/display/display.c`

- [ ] **Step 1: Add component dependency**

Modify `main/CMakeLists.txt` so `loom` is listed in `PRIV_REQUIRES`:

```cmake
PRIV_REQUIRES io vesc connection wireless esc
              nvs_flash esp_timer esp_lcd esp_lcd_st7701 loom
```

- [ ] **Step 2: Include loom in display code**

Add this include to `main/display/display.c`:

```c
#include "loom/loom.h"
```

- [ ] **Step 3: Replace the direct-pixel demo with loom smoke drawing**

Replace the current `display_demo` body with a function that creates `loom`, records a frame, and draws visible primitives:

```c
void display_demo(void) {
  loom_display_config_t cfg = {
      .width = 480,
      .height = 640,
      .format = LOOM_PIXEL_FORMAT_RGB888,
      .tile_height = 64,
      .buffer_count = 2,
      .command_capacity = 128,
      .panel = dpi_panel,
  };

  loom_t *gfx = NULL;
  ESP_ERROR_CHECK(loom_create(&cfg, &gfx));
  ESP_ERROR_CHECK(loom_begin_frame(gfx));
  ESP_ERROR_CHECK(loom_clear(gfx, loom_rgb(8, 10, 12)));
  ESP_ERROR_CHECK(loom_fill_rect(gfx, loom_rect(24, 24, 432, 96),
                                 loom_rgb(20, 48, 70)));
  ESP_ERROR_CHECK(loom_fill_round_rect(gfx, loom_rect(48, 152, 384, 120), 24,
                                       loom_rgb(220, 235, 245)));
  loom_stroke_t stroke = {.width = 4, .color = loom_rgb(255, 196, 72)};
  ESP_ERROR_CHECK(loom_stroke_rect(gfx, loom_rect(72, 308, 336, 96),
                                   &stroke));
  ESP_ERROR_CHECK(loom_draw_line(gfx, (loom_point_t){32, 600},
                                 (loom_point_t){448, 456}, &stroke));
  ESP_ERROR_CHECK(loom_end_frame(gfx));
  loom_destroy(gfx);
}
```

Then update `display_init` to call `display_demo();` and remove the unused `buf` allocation from the old direct-pixel demo.

- [ ] **Step 4: Build the integrated firmware**

Run:

```bash
idf.py build
```

Expected: app compiles and links with `loom`; no unused-parameter or incompatible-signature errors remain.

- [ ] **Step 5: Commit**

Run:

```bash
git add main/CMakeLists.txt main/display/display.c
git commit -m "feat: draw display smoke demo with loom" -m "Wire the new loom graphics component into the existing display initialization path and replace the direct-pixel demo with a tiled RGB888 primitive smoke frame."
```

## Task 6: Add Basic Runtime Diagnostics

**Files:**
- Modify: `components/loom/src/loom.c`

- [ ] **Step 1: Add render timing logs**

In `loom.c`, include:

```c
#include "esp_log.h"
#include "esp_timer.h"
```

Add:

```c
static const char *TAG = "loom";
```

In `loom_end_frame`, capture `esp_timer_get_time()` before rendering begins and after the tile loop ends, then log:

```c
ESP_LOGD(TAG, "rendered %u commands in %lld us",
         (unsigned)loom->command_count, elapsed_us);
```

- [ ] **Step 2: Build after diagnostics**

Run:

```bash
idf.py build
```

Expected: compile succeeds. Logging uses `%lld` with an explicitly `long long` value if the compiler warns.

- [ ] **Step 3: Commit**

Run:

```bash
git add components/loom/src/loom.c
git commit -m "feat: log loom frame timing" -m "Add debug timing around tiled frame rendering so primitive and flush performance can be measured on the ESP32-P4 target."
```

## Task 7: Final Verification

**Files:**
- Read-only verification unless fixes are required.

- [ ] **Step 1: Run full build**

Run:

```bash
idf.py build
```

Expected: firmware builds successfully. If ESP-IDF is unavailable in the shell, report the exact missing environment error.

- [ ] **Step 2: Inspect final diff**

Run:

```bash
git status --short
git diff --stat HEAD
```

Expected: only intentional loom/display integration files remain modified or committed. Pre-existing user changes remain untouched unless they were required for the display integration.

- [ ] **Step 3: Report remaining hardware validation**

Report that visual verification still requires flashing to the ESP32-P4 display. If a serial port is available and the user approves, run:

```bash
idf.py flash monitor
```

Expected on hardware: dark background, blue header rectangle, light rounded rectangle, yellow stroked rectangle, and yellow diagonal line rendered through tiled RGB888 buffers.

