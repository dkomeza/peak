#include "loom_internal.h"

#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"

static const char *TAG = "loom";

#define LOOM_TILE_INTERNAL_CAPS \
  (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
#define LOOM_TILE_SPIRAM_CAPS \
  (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)

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

static void loom_free_tile_buffers(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  for (uint8_t i = 0; i < 2; ++i) {
    heap_caps_free(loom->tile_buffers[i]);
    loom->tile_buffers[i] = NULL;
    loom->tile_buffers_internal[i] = false;
  }
}

static void loom_free_allocations(loom_t *loom) {
  if (loom == NULL) {
    return;
  }

  if (loom->panel_callbacks_registered && loom->config.panel != NULL) {
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {0};
    esp_lcd_dpi_panel_register_event_callbacks(loom->config.panel, &callbacks,
                                               NULL);
    loom->panel_callbacks_registered = false;
  }

  loom_release_frame_texts(loom);
  loom_free_tile_buffers(loom);

  heap_caps_free(loom->commands);
  loom->commands = NULL;

  if (loom->ppa_fill_client != NULL) {
    ppa_unregister_client(loom->ppa_fill_client);
    loom->ppa_fill_client = NULL;
  }

  if (loom->trans_done_sem != NULL) {
    vSemaphoreDelete(loom->trans_done_sem);
    loom->trans_done_sem = NULL;
  }
}

static bool loom_alloc_tile_buffers(loom_t *loom, uint8_t count,
                                    uint32_t caps) {
  if (loom == NULL || count == 0 || count > 2) {
    return false;
  }

  loom_free_tile_buffers(loom);
  for (uint8_t i = 0; i < count; ++i) {
    loom->tile_buffers[i] =
        heap_caps_aligned_alloc(LOOM_TILE_ALIGNMENT, loom->tile_bytes, caps);
    if (loom->tile_buffers[i] == NULL) {
      loom_free_tile_buffers(loom);
      return false;
    }
    loom->tile_buffers_internal[i] = esp_ptr_internal(loom->tile_buffers[i]);
  }
  loom->buffer_count = count;
  return true;
}

static esp_err_t loom_alloc_preferred_tile_buffers(loom_t *loom,
                                                  uint8_t requested_count) {
  size_t internal_largest_before =
      heap_caps_get_largest_free_block(LOOM_TILE_INTERNAL_CAPS);
  size_t internal_free_before = heap_caps_get_free_size(LOOM_TILE_INTERNAL_CAPS);

  if (loom_alloc_tile_buffers(loom, requested_count, LOOM_TILE_INTERNAL_CAPS)) {
    ESP_LOGI(TAG,
             "tile buffers: %u x %u bytes in internal SRAM "
             "(internal free=%u largest=%u before alloc)",
             (unsigned)loom->buffer_count, (unsigned)loom->tile_bytes,
             (unsigned)internal_free_before, (unsigned)internal_largest_before);
    return ESP_OK;
  }

  if (requested_count > 1 &&
      loom_alloc_tile_buffers(loom, 1, LOOM_TILE_INTERNAL_CAPS)) {
    loom->config.buffer_count = 1;
    ESP_LOGW(TAG,
             "only one %u byte tile buffer fit in internal SRAM; using one "
             "buffer instead of %u, so tile flushes cannot be pipelined "
             "(internal free=%u largest=%u before alloc)",
             (unsigned)loom->tile_bytes, (unsigned)requested_count,
             (unsigned)internal_free_before, (unsigned)internal_largest_before);
    return ESP_OK;
  }

  if (loom_alloc_tile_buffers(loom, requested_count, LOOM_TILE_SPIRAM_CAPS)) {
    ESP_LOGW(TAG,
             "tile buffers: %u x %u bytes in PSRAM; CPU raster writes will be "
             "slower (internal free=%u largest=%u before alloc)",
             (unsigned)loom->buffer_count, (unsigned)loom->tile_bytes,
             (unsigned)internal_free_before, (unsigned)internal_largest_before);
    return ESP_OK;
  }

  return ESP_ERR_NO_MEM;
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

  loom_t *loom =
      heap_caps_calloc(1, sizeof(*loom), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
  loom->commands =
      heap_caps_calloc(loom->command_capacity, sizeof(loom->commands[0]),
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (loom->commands == NULL) {
    loom_destroy(loom);
    return ESP_ERR_NO_MEM;
  }

  if (!loom_mul_size_checked(loom->config.width, LOOM_RGB888_BYTES_PER_PIXEL,
                             &loom->tile_stride) ||
      !loom_mul_size_checked(loom->tile_stride, loom->config.tile_height,
                             &loom->tile_bytes) ||
      loom->tile_stride == 0 || loom->tile_bytes == 0) {
    loom_destroy(loom);
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t alloc_ret =
      loom_alloc_preferred_tile_buffers(loom, loom->config.buffer_count);
  if (alloc_ret != ESP_OK) {
    loom_destroy(loom);
    return alloc_ret;
  }

  ppa_client_config_t ppa_config = {
      .oper_type = PPA_OPERATION_FILL,
      .max_pending_trans_num = 1,
  };
  esp_err_t ppa_ret = ppa_register_client(&ppa_config, &loom->ppa_fill_client);
  if (ppa_ret != ESP_OK) {
    ESP_LOGW(TAG, "PPA fill unavailable, using CPU fills: %s",
             esp_err_to_name(ppa_ret));
    loom->ppa_fill_client = NULL;
  }

  esp_lcd_dpi_panel_event_callbacks_t callbacks = {
      .on_color_trans_done = loom_color_trans_done_cb,
  };
  esp_err_t ret = esp_lcd_dpi_panel_register_event_callbacks(loom->config.panel,
                                                             &callbacks, loom);
  if (ret != ESP_OK) {
    loom_destroy(loom);
    return ret;
  }
  loom->panel_callbacks_registered = true;

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

  int64_t start_us = esp_timer_get_time();
  int64_t render_us = 0;
  int64_t flush_us = 0;
  uint32_t tile_count = 0;
  loom_rect_t dirty = loom_rect(0, 0, 0, 0);
  esp_err_t ret = loom->sticky_error;
  if (ret == ESP_OK) {
    dirty = loom->dirty_valid ? loom->dirty : loom_screen_rect(loom);
    dirty = loom_clip_to_screen(loom, dirty);

    int dirty_bottom = dirty.y + dirty.h;
    uint8_t buffer_index = 0;
    bool flush_pending = false;
    for (int y = dirty.y; !loom_rect_is_empty(dirty) && y < dirty_bottom;
         y += loom->config.tile_height) {
      int remaining = dirty_bottom - y;
      int tile_h = remaining < loom->config.tile_height
                       ? remaining
                       : loom->config.tile_height;
      loom_rect_t tile_rect = loom_rect(dirty.x, y, dirty.w, tile_h);
      uint8_t *tile = loom->tile_buffers[buffer_index];

      int64_t render_start_us = esp_timer_get_time();
      ret = loom_render_tile(loom, tile, tile_rect);
      render_us += esp_timer_get_time() - render_start_us;
      if (ret != ESP_OK) {
        break;
      }

      int64_t flush_start_us = esp_timer_get_time();
      if (loom->buffer_count > 1) {
        if (flush_pending) {
          ret = loom_backend_flush_wait(loom);
          flush_us += esp_timer_get_time() - flush_start_us;
          flush_pending = false;
          if (ret != ESP_OK) {
            break;
          }
          flush_start_us = esp_timer_get_time();
        }

        ret = loom_backend_flush_start(loom, tile, tile_rect);
        flush_us += esp_timer_get_time() - flush_start_us;
        if (ret == ESP_OK) {
          flush_pending = true;
        }
      } else {
        ret = loom_backend_flush(loom, tile, tile_rect);
        flush_us += esp_timer_get_time() - flush_start_us;
      }
      if (ret != ESP_OK) {
        break;
      }

      tile_count++;
      buffer_index = (uint8_t)((buffer_index + 1) % loom->buffer_count);
    }

    if (flush_pending) {
      int64_t flush_start_us = esp_timer_get_time();
      esp_err_t wait_ret = loom_backend_flush_wait(loom);
      flush_us += esp_timer_get_time() - flush_start_us;
      if (ret == ESP_OK) {
        ret = wait_ret;
      }
    }
  }

  loom_release_frame_texts(loom);
  loom->in_frame = false;
  int64_t elapsed_us = esp_timer_get_time() - start_us;
  ESP_LOGI(TAG,
           "rendered %u commands, %u tiles, dirty=%dx%d in %lld us "
           "(render=%lld us flush=%lld us), ~%f hz",
           (unsigned)loom->command_count, (unsigned)tile_count, dirty.w,
           dirty.h, (long long)elapsed_us, (long long)render_us,
           (long long)flush_us, (float)1000000 / elapsed_us);
  return ret;
}
