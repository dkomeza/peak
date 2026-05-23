#include "loom_internal.h"

#include "esp_lcd_panel_ops.h"

esp_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                             loom_rect_t tile_rect) {
  if (loom == NULL || tile == NULL || loom->config.panel == NULL ||
      loom_rect_is_empty(tile_rect)) {
    return ESP_ERR_INVALID_ARG;
  }

  return esp_lcd_panel_draw_bitmap(loom->config.panel, tile_rect.x,
                                   tile_rect.y, tile_rect.x + tile_rect.w,
                                   tile_rect.y + tile_rect.h, tile);
}
