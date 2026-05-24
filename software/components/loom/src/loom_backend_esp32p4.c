#include "loom_internal.h"

#include "esp_lcd_panel_ops.h"

esp_err_t loom_backend_flush_start(loom_t *loom, const uint8_t *tile,
                                   loom_rect_t tile_rect) {
  if (loom == NULL || tile == NULL || loom->config.panel == NULL ||
      loom->trans_done_sem == NULL || loom_rect_is_empty(tile_rect)) {
    return ESP_ERR_INVALID_ARG;
  }

  while (xSemaphoreTake(loom->trans_done_sem, 0) == pdTRUE) {
  }

  return esp_lcd_panel_draw_bitmap(loom->config.panel, tile_rect.x, tile_rect.y,
                                   tile_rect.x + tile_rect.w,
                                   tile_rect.y + tile_rect.h, tile);
}

esp_err_t loom_backend_flush_wait(loom_t *loom) {
  if (loom == NULL || loom->trans_done_sem == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(loom->trans_done_sem, portMAX_DELAY) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}

esp_err_t loom_backend_flush(loom_t *loom, const uint8_t *tile,
                             loom_rect_t tile_rect) {
  esp_err_t ret = loom_backend_flush_start(loom, tile, tile_rect);
  if (ret != ESP_OK) {
    return ret;
  }

  return loom_backend_flush_wait(loom);
}
