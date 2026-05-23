# Loom Graphics Library Design

Date: 2026-05-23

## Summary

`loom` is a lightweight immediate-mode graphics library for the ESP32-P4
firmware. It provides fast drawing primitives and text rendering while keeping
memory use predictable through renderer-owned partial screen buffers. It is
intended to become the low-level rendering layer under a future retained-mode,
React Native-like UI system.

The first backend targets the current display stack:

- ESP32-P4
- ESP-IDF 5.5.1 or newer
- ST7701 panel over MIPI DSI/DPI
- 480x640 RGB888 output
- PPA and DMA2D used where they map cleanly to the work

## Goals

- Provide an immediate-mode drawing API for UI primitives and text.
- Keep the public API independent of the internal pixel format where practical.
- Render through partial RGB888 tile buffers, around one tenth of the screen by
  default.
- Support double buffering of partial tiles so rendering and panel transfer can
  overlap.
- Use ESP32-P4 PPA and DMA2D opportunistically for fills, blends, bitmap
  movement, and LCD transfer.
- Keep CPU fallbacks for primitives that do not map well to hardware
  acceleration.
- Make the command model usable by a future retained-mode UI layer.

## Non-Goals

- `loom` will not be a widget toolkit.
- `loom` will not own display initialization.
- `loom` will not parse TTF, OTF, or other font files at runtime.
- `loom` will not require a full-screen framebuffer for the initial
  implementation.
- `loom` will not attempt to accelerate every primitive through PPA.

## Component Layout

`loom` will live alongside the existing firmware as an ESP-IDF component:

```text
components/loom/
  CMakeLists.txt
  include/loom/loom.h
  include/loom/types.h
  include/loom/font.h
  src/loom.c
  src/loom_command.c
  src/loom_raster.c
  src/loom_backend_esp32p4.c
```

The existing `main/display` code remains responsible for panel setup. After it
creates the `esp_lcd_panel_handle_t`, it passes the panel handle and screen
configuration into `loom`.

## Architecture

`loom` uses an immediate-mode API that records draw commands during a frame:

```c
loom_begin_frame(gfx);
loom_fill_rect(gfx, rect, color);
loom_draw_line(gfx, p0, p1, stroke);
loom_draw_text(gfx, font, "Speed", x, y, &style);
loom_end_frame(gfx);
```

The renderer processes commands at `loom_end_frame`. It walks dirty regions in
fixed-height RGB888 tiles, renders commands that intersect the active tile, and
flushes each tile to the display. Two tile buffers allow one tile to be rendered
while the previous tile is being transferred when the backend can do so safely.

The core flow is:

```text
future retained UI layer
  -> loom immediate-mode commands
  -> command list with bounds
  -> tiled RGB888 renderer
  -> ESP32-P4 backend
  -> esp_lcd_panel_draw_bitmap / DMA2D
```

This keeps the first implementation small while preserving a clean handoff point
for a retained UI layer later.

## Display And Buffer Ownership

`loom` owns partial-buffer allocation and tiling. Callers provide display
configuration, not per-tile buffers, for normal rendering.

Example configuration:

```c
loom_display_config_t cfg = {
    .width = 480,
    .height = 640,
    .format = LOOM_PIXEL_FORMAT_RGB888,
    .tile_height = 64,
    .buffer_count = 2,
    .panel = dpi_panel,
};

loom_t *gfx = NULL;
ESP_ERROR_CHECK(loom_create(&cfg, &gfx));
```

For the current 480x640 RGB888 panel:

- Full screen: `480 * 640 * 3 = 921600` bytes.
- One 480x64 tile: `480 * 64 * 3 = 92160` bytes.
- Two 480x64 tiles: `184320` bytes.

The default tile height should target one tenth of the screen. Tile height must
remain configurable so the project can tune SRAM use, alignment, and
performance.

An advanced caller-provided canvas mode may be added later for tests, special
effects, or offscreen rendering, but the normal app path should use
renderer-owned tiles.

## Pixel Formats

RGB888 is the first-class internal and output format for the initial backend.
This matches the current panel configuration and preserves quality for
antialiased geometry and text.

The public API uses format-neutral types such as `loom_color_t` and
`loom_rect_t`. A later RGB565 backend can be added behind the same API for
reduced memory and bandwidth. RGB565 should not leak into the initial public API
except as an optional pixel format enum in configuration.

## Public Drawing API

Initial primitives:

- `loom_clear`
- `loom_fill_rect`
- `loom_stroke_rect`
- `loom_fill_round_rect`
- `loom_stroke_round_rect`
- `loom_draw_line`
- `loom_draw_arc`
- `loom_draw_bitmap`
- `loom_draw_text`
- `loom_push_clip`
- `loom_pop_clip`
- `loom_invalidate_rect`

Implementation priority:

1. Solid rectangles and bitmap blits.
2. Lines and stroked rectangles.
3. Rounded rectangles.
4. Text.
5. Arcs.

Rectangles, blits, and text are expected to dominate the future UI layer. Arcs
are useful but more specialized and harder to accelerate directly, so they come
after the core UI primitives.

## Command List

Each draw call appends a command to the current frame command list. Every
command stores:

- Command type.
- Draw parameters.
- Conservative bounds in screen coordinates.
- Active clip rectangle.
- Style data or references to immutable style data.

During tile rendering, commands whose bounds do not intersect the tile are
skipped. Intersecting commands are clipped to tile-local coordinates before
rasterization. This makes partial-buffer rendering deterministic and keeps
tiling mechanics out of the future UI layer.

The first command list implementation can use a fixed-capacity array configured
at `loom_create`. If the frame exceeds capacity, drawing should fail with a
clear error rather than silently dropping commands.

## Dirty Regions

Dirty regions are explicit but optional.

- If no region is invalidated, `loom_end_frame` may redraw the full screen.
- Callers can use `loom_invalidate_rect` to restrict redraw work.
- The command list can also maintain a simple union of command bounds for early
  dirty-region support.

The first implementation can start with one unioned dirty rectangle. Multiple
dirty rectangles can be added later if profiling shows it matters.

## ESP32-P4 Acceleration

The ESP32-P4 backend uses accelerators where they are a natural fit:

- PPA fill for large solid fills inside a tile.
- PPA blend for bitmap alpha masks, glyph masks, icons, and cached layers.
- PPA scale/rotate/mirror for future bitmap transforms where needed.
- DMA2D and the ESP LCD panel path for copying rendered tiles into the display
  pipeline.
- CPU rasterization for lines, rounded geometry, arcs, clipping, and small
  operations where accelerator setup cost is not worthwhile.

The backend exposes capability flags such as:

- `LOOM_CAP_PPA_FILL`
- `LOOM_CAP_PPA_BLEND`
- `LOOM_CAP_PPA_SRM`
- `LOOM_CAP_DMA2D_FLUSH`

Acceleration is an implementation detail. Public drawing behavior must remain
the same when a path falls back to CPU rendering.

Tile buffers must respect ESP32-P4 DMA and cache alignment requirements. The
current project uses a 64-byte L2 cache line size, so tile buffers should be
64-byte aligned. The backend should also preserve any 16-byte alignment needed
by DMA2D or encrypted external memory paths.

The backend should use size thresholds. For small fills, glyphs, and clips,
direct CPU rasterization may be faster than scheduling a PPA transaction.

## Rasterization

The initial CPU rasterizer should favor predictable integer math.

- Solid fills write directly into the RGB888 tile.
- Lines use a clipped integer algorithm, with antialiasing added after the
  basic path works.
- Rounded rectangles and arcs generate coverage at edges and composite into the
  tile.
- Bitmap and glyph paths composite an 8-bit coverage or alpha mask into RGB888.

Antialiasing should be part of the design, but not every primitive needs perfect
antialiasing in the first milestone. Text quality and rounded geometry should be
prioritized first.

## Text And Fonts

`loom` uses a font abstraction rather than a hardcoded font format:

```c
typedef struct loom_font loom_font_t;

esp_err_t loom_draw_text(
    loom_t *loom,
    const loom_font_t *font,
    const char *text,
    int x,
    int y,
    const loom_text_style_t *style);
```

Font data is generated offline. Firmware should not parse scalable font files
or rasterize outlines at runtime.

The initial font backend supports atlas fonts:

- UTF-8 text input.
- A limited glyph set at first, likely ASCII plus selected UI symbols.
- Fallback glyph for missing characters.
- Per-glyph metrics: advance, bearing, atlas rectangle, and dimensions.
- Optional kerning in a later milestone.

Two atlas modes should be supported by the design:

- A8 coverage atlas: best quality at one intended size and simple to composite.
- SDF A8 atlas: scalable over a limited size range and useful for outlines,
  shadows, and other effects.

SDF is the preferred scalable direction, but it is not automatically better for
small UI text. The API should allow either A8 or SDF atlases so the project can
choose based on visual quality and performance measurements.

Glyph rendering samples an 8-bit coverage value and blends foreground RGB into
the RGB888 tile. PPA blend may be used for whole glyph spans or glyph bitmaps
when profitable. CPU fallback handles clipping, small glyphs, and any cases PPA
cannot express cleanly.

## Error Handling

`loom` returns `esp_err_t` from operations that allocate, flush, or otherwise
can fail.

- Allocation failure: `ESP_ERR_NO_MEM`.
- Invalid configuration or geometry: `ESP_ERR_INVALID_ARG`.
- Invalid renderer state: `ESP_ERR_INVALID_STATE`.
- LCD, PPA, or DMA2D failures: propagate the original `esp_err_t` where
  possible.

Draw commands that only append to the command list should return errors when the
command list is full or the frame state is invalid. The first implementation can
choose between return-value-heavy APIs and a sticky error model, but it must not
silently drop draw commands.

## Testing And Validation

The first implementation should include:

- Target-independent raster tests where possible for clipping, command bounds,
  color packing, and simple primitives.
- ESP-IDF build verification for the `loom` component and the firmware app.
- An on-device smoke demo that draws rectangles, lines, rounded rectangles,
  bitmap/glyph samples, and text through partial buffers.
- Basic timing logs for tile render time and flush time.

Verification should focus on correctness first, then accelerator usage and
performance.

## External References

- ESP-IDF PPA documentation:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/ppa.html
- ESP-IDF MIPI DSI LCD documentation:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html
