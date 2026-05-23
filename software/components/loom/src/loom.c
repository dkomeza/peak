#include "loom_internal.h"

#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"

static bool IRAM_ATTR loom_color_trans_done_cb(
    esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
    void *user_ctx) {
  (void)panel;
  (void)edata;

  loom_t *loom = (loom_t *)user_ctx;
  if (loom == NULL || loom->trans_done_sem == NULL) {
    return false;
  }

  BaseType_t need_yield = pdFALSE;
  xSemaphoreGiveFromISR(loom->trans_done_sem, &need_yield);
  return need_yield == pdTRUE;
}

static uint16_t loom_default_tile_height(uint16_t height) {
  uint16_t tile_height = height / 10;
  return tile_height > 0 ? tile_height : 1;
}

static void loom_free_allocations(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  loom_release_frame_texts(loom);

  for (uint8_t i = 0; i < 2; ++i) {
    heap_caps_free(loom->tile_buffers[i]);
    loom->tile_buffers[i] = NULL;
  }

  heap_caps_free(loom->commands);
  loom->commands = NULL;

  if (loom->trans_done_sem != NULL) {
    vSemaphoreDelete(loom->trans_done_sem);
    loom->trans_done_sem = NULL;
  }
}

static uint8_t *loom_alloc_tile_buffer(size_t bytes) {
  uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
  uint8_t *buffer = heap_caps_aligned_alloc(LOOM_TILE_ALIGNMENT, bytes,
                                            internal_caps);
  if (buffer != NULL) {
    return buffer;
  }

  uint32_t spiram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
  return heap_caps_aligned_alloc(LOOM_TILE_ALIGNMENT, bytes, spiram_caps);
}

static bool loom_mul_size_checked(size_t a, size_t b, size_t *out) {
  if (out == NULL) {
    return false;
  }
  if (a != 0 && b > SIZE_MAX / a) {
    return false;
  }

  *out = a * b;
  return true;
}

esp_err_t loom_create(const loom_display_config_t *config, loom_t **out_loom) {
  if (config == NULL || out_loom == NULL || config->panel == NULL ||
      config->width == 0 || config->height == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  *out_loom = NULL;

  if (config->format != LOOM_PIXEL_FORMAT_RGB888) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  loom_t *loom = heap_caps_calloc(1, sizeof(*loom),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (loom == NULL) {
    return ESP_ERR_NO_MEM;
  }

  loom->config = *config;
  if (loom->config.tile_height == 0) {
    loom->config.tile_height = loom_default_tile_height(config->height);
  }
  if (loom->config.tile_height == 0) {
    loom_destroy(loom);
    return ESP_ERR_INVALID_ARG;
  }

  loom->trans_done_sem = xSemaphoreCreateBinary();
  if (loom->trans_done_sem == NULL) {
    loom_destroy(loom);
    return ESP_ERR_NO_MEM;
  }

  loom->config.buffer_count = config->buffer_count >= 2 ? 2 : 1;
  loom->config.command_capacity = config->command_capacity > 0
                                      ? config->command_capacity
                                      : LOOM_DEFAULT_COMMAND_CAPACITY;

  loom->command_capacity = loom->config.command_capacity;
  loom->commands = heap_caps_calloc(loom->command_capacity,
                                    sizeof(loom->commands[0]),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (loom->commands == NULL) {
    loom_destroy(loom);
    return ESP_ERR_NO_MEM;
  }

  loom->buffer_count = loom->config.buffer_count;
  if (!loom_mul_size_checked(loom->config.width, LOOM_RGB888_BYTES_PER_PIXEL,
                             &loom->tile_stride) ||
      !loom_mul_size_checked(loom->tile_stride, loom->config.tile_height,
                             &loom->tile_bytes) ||
      loom->tile_stride == 0 || loom->tile_bytes == 0) {
    loom_destroy(loom);
    return ESP_ERR_INVALID_ARG;
  }

  for (uint8_t i = 0; i < loom->buffer_count; ++i) {
    loom->tile_buffers[i] = loom_alloc_tile_buffer(loom->tile_bytes);
    if (loom->tile_buffers[i] == NULL) {
      loom_destroy(loom);
      return ESP_ERR_NO_MEM;
    }
  }

  esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_color_trans_done = loom_color_trans_done_cb,
  };
  esp_err_t ret = esp_lcd_dpi_panel_register_event_callbacks(
      loom->config.panel, &callbacks, loom);
  if (ret != ESP_OK) {
    loom_destroy(loom);
    return ret;
  }

  *out_loom = loom;
  return ESP_OK;
}

void loom_destroy(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  loom_free_allocations(loom);
  memset(loom, 0, sizeof(*loom));
  heap_caps_free(loom);
}

esp_err_t loom_begin_frame(loom_t *loom) {
  if (loom == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }

  loom_release_frame_texts(loom);
  loom->command_count = 0;
  loom->clip_stack[0] = loom_screen_rect(loom);
  loom->clip_depth = 1;
  loom->dirty = loom_rect(0, 0, 0, 0);
  loom->dirty_valid = false;
  loom->sticky_error = ESP_OK;
  loom->in_frame = true;

  return ESP_OK;
}

esp_err_t loom_end_frame(loom_t *loom) {
  if (loom == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!loom->in_frame) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = loom->sticky_error;
  if (ret != ESP_OK) {
    loom_release_frame_texts(loom);
    loom->in_frame = false;
    return ret;
  }

  loom_rect_t dirty = loom->dirty_valid ? loom->dirty : loom_screen_rect(loom);
  dirty = loom_clip_to_screen(loom, dirty);
  if (loom_rect_is_empty(dirty)) {
    loom_release_frame_texts(loom);
    loom->in_frame = false;
    return ESP_OK;
  }

  int dirty_bottom = dirty.y + dirty.h;
  uint8_t buffer_index = 0;
  for (int y = dirty.y; y < dirty_bottom; y += loom->config.tile_height) {
    int remaining = dirty_bottom - y;
    int tile_h = remaining < loom->config.tile_height
                     ? remaining
                     : loom->config.tile_height;
    loom_rect_t tile_rect = loom_rect(dirty.x, y, dirty.w, tile_h);
    uint8_t *tile = loom->tile_buffers[buffer_index];

    ret = loom_render_tile(loom, tile, tile_rect);
    if (ret != ESP_OK) {
      break;
    }

    ret = loom_backend_flush(loom, tile, tile_rect);
    if (ret != ESP_OK) {
      break;
    }

    buffer_index = (uint8_t)((buffer_index + 1) % loom->buffer_count);
  }

  loom_release_frame_texts(loom);
  loom->in_frame = false;
  return ret;
}
