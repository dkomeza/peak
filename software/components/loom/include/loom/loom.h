#ifndef LOOM_H
#define LOOM_H

#include "esp_err.h"
#include "loom/font.h"
#include "loom/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct loom loom_t;

/**
 * Create a loom renderer for a DPI panel.
 *
 * The ESP32-P4 backend registers DPI panel transfer callbacks while the
 * renderer is alive so it can wait until draw buffers are safe to reuse.
 * Callers should not register competing DPI callbacks on the same panel during
 * the renderer lifetime. loom_destroy() clears the callbacks before releasing
 * renderer resources.
 */
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
