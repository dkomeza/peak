#include "loom_benchmark.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "loom/fonts.h"
#include "loom/loom.h"
#include "loom/loom_esp_idf.h"
#include "sdkconfig.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if CONFIG_PEAK_LOOM_BENCHMARK

#define BENCH_DISPLAY_WIDTH 480
#define BENCH_DISPLAY_HEIGHT 640
#define BENCH_TILE_HEIGHT 64
#define BENCH_BUFFER_COUNT 2
#define BENCH_COMMAND_CAPACITY 256
#define BENCH_BITMAP_SIZE 64

#ifndef CONFIG_PEAK_LOOM_BENCHMARK_WARMUP_FRAMES
#define CONFIG_PEAK_LOOM_BENCHMARK_WARMUP_FRAMES 3
#endif

#ifndef CONFIG_PEAK_LOOM_BENCHMARK_SAMPLES
#define CONFIG_PEAK_LOOM_BENCHMARK_SAMPLES 8
#endif

#ifndef CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS
#define CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS 5
#endif

static const char *TAG = "loom_bench";

typedef loom_err_t (*loom_bench_draw_fn_t)(loom_t *gfx, int frame);

typedef struct {
  const char *name;
  loom_bench_draw_fn_t draw;
} loom_bench_case_t;

typedef struct {
  int64_t min_us;
  int64_t max_us;
  int64_t total_us;
  uint32_t samples;
} loom_bench_stats_t;

typedef struct {
  uint8_t *rgb888;
  uint8_t *rgba8888;
  uint8_t *a8;
} loom_bench_bitmaps_t;

static loom_bench_bitmaps_t s_bitmaps;

static loom_err_t bench_invalidate_full(loom_t *gfx) {
  return loom_invalidate_rect(
      gfx, loom_rect(0, 0, BENCH_DISPLAY_WIDTH, BENCH_DISPLAY_HEIGHT));
}

static loom_err_t bench_ret(loom_err_t ret, loom_err_t next) {
  return ret == LOOM_OK ? next : ret;
}

static void bench_free_bitmaps(void) {
  heap_caps_free(s_bitmaps.rgb888);
  heap_caps_free(s_bitmaps.rgba8888);
  heap_caps_free(s_bitmaps.a8);
  s_bitmaps = (loom_bench_bitmaps_t){0};
}

static uint8_t *bench_alloc_bitmap(size_t size) {
  uint8_t *pixels = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pixels == NULL) {
    pixels = heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  return pixels;
}

static esp_err_t bench_init_bitmaps(void) {
  size_t rgb888_size = BENCH_BITMAP_SIZE * BENCH_BITMAP_SIZE * 3;
  size_t rgba8888_size = BENCH_BITMAP_SIZE * BENCH_BITMAP_SIZE * 4;
  size_t a8_size = BENCH_BITMAP_SIZE * BENCH_BITMAP_SIZE;

  s_bitmaps.rgb888 = bench_alloc_bitmap(rgb888_size);
  s_bitmaps.rgba8888 = bench_alloc_bitmap(rgba8888_size);
  s_bitmaps.a8 = bench_alloc_bitmap(a8_size);
  if (s_bitmaps.rgb888 == NULL || s_bitmaps.rgba8888 == NULL ||
      s_bitmaps.a8 == NULL) {
    bench_free_bitmaps();
    return ESP_ERR_NO_MEM;
  }

  for (int y = 0; y < BENCH_BITMAP_SIZE; ++y) {
    for (int x = 0; x < BENCH_BITMAP_SIZE; ++x) {
      size_t i = (size_t)y * BENCH_BITMAP_SIZE + x;
      uint8_t r = (uint8_t)((x * 255) / (BENCH_BITMAP_SIZE - 1));
      uint8_t g = (uint8_t)((y * 255) / (BENCH_BITMAP_SIZE - 1));
      uint8_t b = (uint8_t)(((x + y) * 255) / ((BENCH_BITMAP_SIZE - 1) * 2));
      uint8_t a = (uint8_t)(((x ^ y) * 255) / (BENCH_BITMAP_SIZE - 1));

      s_bitmaps.rgb888[i * 3 + 0] = r;
      s_bitmaps.rgb888[i * 3 + 1] = g;
      s_bitmaps.rgb888[i * 3 + 2] = b;

      s_bitmaps.rgba8888[i * 4 + 0] = b;
      s_bitmaps.rgba8888[i * 4 + 1] = r;
      s_bitmaps.rgba8888[i * 4 + 2] = g;
      s_bitmaps.rgba8888[i * 4 + 3] = a;

      s_bitmaps.a8[i] = a;
    }
  }

  return ESP_OK;
}

static loom_err_t bench_clear(loom_t *gfx, int frame) {
  uint8_t c = (uint8_t)(frame & 0x1f);
  return loom_clear(gfx, loom_rgb(3 + c, 6, 10));
}

static loom_err_t bench_fill_rect(loom_t *gfx, int frame) {
  (void)frame;
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_rect(gfx, loom_rect(32, 72, 416, 496),
                                       loom_rgb(38, 160, 220)));
}

static loom_err_t bench_fill_rect_alpha(loom_t *gfx, int frame) {
  (void)frame;
  loom_err_t ret = bench_invalidate_full(gfx);
  ret = bench_ret(ret, loom_fill_rect(gfx, loom_rect(0, 0, BENCH_DISPLAY_WIDTH,
                                                    BENCH_DISPLAY_HEIGHT),
                                      loom_rgb(3, 5, 8)));
  return bench_ret(ret, loom_fill_rect(gfx, loom_rect(32, 72, 416, 496),
                                       loom_rgba(240, 88, 48, 132)));
}

static loom_err_t bench_fill_rect_linear_gradient(loom_t *gfx, int frame) {
  (void)frame;
  loom_linear_gradient_t gradient = {
      .p0 = {0, 0},
      .p1 = {BENCH_DISPLAY_WIDTH, BENCH_DISPLAY_HEIGHT},
      .color0 = loom_rgb(10, 42, 66),
      .color1 = loom_rgb(248, 132, 40),
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_rect_linear_gradient(
                            gfx, loom_rect(0, 0, BENCH_DISPLAY_WIDTH,
                                           BENCH_DISPLAY_HEIGHT),
                            &gradient));
}

static loom_err_t bench_stroke_rect(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 9, .color = loom_rgb(235, 238, 240)};
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret,
                   loom_stroke_rect(gfx, loom_rect(38, 80, 404, 480),
                                    &stroke));
}

static loom_err_t bench_fill_round_rect(loom_t *gfx, int frame) {
  (void)frame;
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_round_rect(gfx, loom_rect(38, 80, 404, 480),
                                             42, loom_rgb(42, 180, 120)));
}

static loom_err_t bench_fill_round_rect_gradient(loom_t *gfx, int frame) {
  (void)frame;
  loom_linear_gradient_t gradient = {
      .p0 = {38, 80},
      .p1 = {442, 560},
      .color0 = loom_rgba(38, 190, 220, 240),
      .color1 = loom_rgba(250, 210, 82, 220),
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_round_rect_linear_gradient(
                            gfx, loom_rect(38, 80, 404, 480), 42, &gradient));
}

static loom_err_t bench_stroke_round_rect(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 8, .color = loom_rgb(250, 210, 82)};
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_stroke_round_rect(
                            gfx, loom_rect(38, 80, 404, 480), 42, &stroke));
}

static loom_err_t bench_fill_circle(loom_t *gfx, int frame) {
  (void)frame;
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_circle(gfx, (loom_point_t){240, 320}, 198,
                                         loom_rgb(84, 182, 255)));
}

static loom_err_t bench_fill_circle_radial_gradient(loom_t *gfx, int frame) {
  (void)frame;
  loom_radial_gradient_t gradient = {
      .center = {240, 320},
      .radius = 210,
      .color0 = loom_rgba(255, 255, 255, 235),
      .color1 = loom_rgba(30, 90, 180, 18),
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_fill_circle_radial_gradient(
                            gfx, (loom_point_t){240, 320}, 198, &gradient));
}

static loom_err_t bench_stroke_circle(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 18, .color = loom_rgb(255, 255, 255)};
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_stroke_circle(gfx, (loom_point_t){240, 320}, 198,
                                           &stroke));
}

static loom_err_t bench_line(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 7, .color = loom_rgb(255, 210, 80)};
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_line(gfx, (loom_point_t){24, 602},
                                       (loom_point_t){456, 38}, &stroke));
}

static loom_err_t bench_arc(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 20, .color = loom_rgb(235, 238, 240)};
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_arc(gfx, (loom_point_t){240, 320}, 190, 135,
                                      270, &stroke));
}

static loom_err_t bench_arc_gradient_sweep(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 20, .color = loom_rgb(255, 255, 255)};
  loom_arc_gradient_t gradient = {
      .mode = LOOM_ARC_GRADIENT_SWEEP,
      .color0 = loom_rgb(86, 190, 255),
      .color1 = loom_rgb(255, 94, 94),
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_arc_gradient(
                            gfx, (loom_point_t){240, 320}, 190, 135, 270,
                            &stroke, &gradient));
}

static loom_err_t bench_arc_gradient_radial(loom_t *gfx, int frame) {
  (void)frame;
  loom_stroke_t stroke = {.width = 28, .color = loom_rgb(255, 255, 255)};
  loom_arc_gradient_t gradient = {
      .mode = LOOM_ARC_GRADIENT_RADIAL,
      .color0 = loom_rgb(255, 255, 255),
      .color1 = loom_rgb(86, 190, 255),
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_arc_gradient(
                            gfx, (loom_point_t){240, 320}, 184, 135, 270,
                            &stroke, &gradient));
}

static loom_err_t bench_bitmap_rgb888(loom_t *gfx, int frame) {
  (void)frame;
  loom_bitmap_t bitmap = {
      .width = BENCH_BITMAP_SIZE,
      .height = BENCH_BITMAP_SIZE,
      .format = LOOM_BITMAP_FORMAT_RGB888,
      .stride = BENCH_BITMAP_SIZE * 3,
      .pixels = s_bitmaps.rgb888,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_bitmap(gfx, loom_rect(52, 96, 376, 376),
                                         &bitmap, loom_rgb(255, 255, 255)));
}

static loom_err_t bench_bitmap_rgba8888(loom_t *gfx, int frame) {
  (void)frame;
  loom_bitmap_t bitmap = {
      .width = BENCH_BITMAP_SIZE,
      .height = BENCH_BITMAP_SIZE,
      .format = LOOM_BITMAP_FORMAT_RGBA8888,
      .stride = BENCH_BITMAP_SIZE * 4,
      .pixels = s_bitmaps.rgba8888,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_bitmap(gfx, loom_rect(52, 96, 376, 376),
                                         &bitmap, loom_rgb(255, 255, 255)));
}

static loom_err_t bench_bitmap_a8(loom_t *gfx, int frame) {
  (void)frame;
  loom_bitmap_t bitmap = {
      .width = BENCH_BITMAP_SIZE,
      .height = BENCH_BITMAP_SIZE,
      .format = LOOM_BITMAP_FORMAT_A8,
      .stride = BENCH_BITMAP_SIZE,
      .pixels = s_bitmaps.a8,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_bitmap(gfx, loom_rect(52, 96, 376, 376),
                                         &bitmap, loom_rgb(255, 210, 80)));
}

static loom_err_t bench_text_16(loom_t *gfx, int frame) {
  (void)frame;
  loom_text_style_t style = {
      .color = loom_rgb(230, 240, 245),
      .opacity = 255,
      .size_px = 16,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_16,
                                       "PEAK LOOM RENDER BENCHMARK", 28, 300,
                                       &style));
}

static loom_err_t bench_text_32(loom_t *gfx, int frame) {
  (void)frame;
  loom_text_style_t style = {
      .color = loom_rgb(230, 240, 245),
      .opacity = 255,
      .size_px = 32,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_32,
                                       "ROAD PAS 42.8V", 34, 288, &style));
}

static loom_err_t bench_text_digits_96(loom_t *gfx, int frame) {
  (void)frame;
  loom_text_style_t style = {
      .color = loom_rgb(255, 255, 255),
      .opacity = 255,
      .size_px = 96,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_digits_96,
                                       "123456", 62, 250, &style));
}

static loom_err_t bench_text_digits_144(loom_t *gfx, int frame) {
  (void)frame;
  loom_text_style_t style = {
      .color = loom_rgb(255, 255, 255),
      .opacity = 255,
      .size_px = 144,
  };
  loom_err_t ret = bench_invalidate_full(gfx);
  return bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_digits_144,
                                       "48", 142, 220, &style));
}

static loom_err_t bench_clip_stack(loom_t *gfx, int frame) {
  (void)frame;
  loom_err_t ret = bench_invalidate_full(gfx);
  ret = bench_ret(ret, loom_push_clip(gfx, loom_rect(86, 112, 308, 392)));
  ret = bench_ret(ret, loom_push_clip(gfx, loom_rect(130, 160, 220, 300)));
  ret = bench_ret(ret, loom_fill_circle(gfx, (loom_point_t){240, 320}, 210,
                                        loom_rgba(86, 190, 255, 230)));
  ret = bench_ret(ret, loom_pop_clip(gfx));
  ret = bench_ret(ret, loom_pop_clip(gfx));
  return ret;
}

static loom_err_t bench_mixed_scene(loom_t *gfx, int frame) {
  loom_err_t ret = loom_clear(gfx, loom_rgb(4, 6, 9));
  loom_linear_gradient_t background = {
      .p0 = {0, 0},
      .p1 = {0, BENCH_DISPLAY_HEIGHT},
      .color0 = loom_rgb(10, 18, 24),
      .color1 = loom_rgb(2, 5, 8),
  };
  ret = bench_ret(ret, loom_fill_rect_linear_gradient(
                           gfx, loom_rect(0, 0, BENCH_DISPLAY_WIDTH,
                                          BENCH_DISPLAY_HEIGHT),
                           &background));

  loom_point_t center = {240, 248};
  loom_stroke_t track = {.width = 16, .color = loom_rgba(255, 255, 255, 32)};
  ret = bench_ret(ret, loom_draw_arc(gfx, center, 172, 150, 240, &track));

  loom_stroke_t speed_stroke = {.width = 18,
                                .color = loom_rgb(255, 255, 255)};
  loom_arc_gradient_t speed_gradient = {
      .mode = LOOM_ARC_GRADIENT_SWEEP,
      .color0 = loom_rgb(86, 190, 255),
      .color1 = loom_rgb(255, 207, 95),
  };
  int sweep = 80 + (frame % 120);
  ret = bench_ret(ret, loom_draw_arc_gradient(gfx, center, 172, 150, sweep,
                                              &speed_stroke,
                                              &speed_gradient));

  loom_radial_gradient_t glow = {
      .center = center,
      .radius = 118,
      .color0 = loom_rgba(60, 130, 170, 58),
      .color1 = loom_rgba(0, 0, 0, 0),
  };
  ret = bench_ret(ret, loom_fill_circle_radial_gradient(gfx, center, 118,
                                                        &glow));

  loom_text_style_t digits = {
      .color = loom_rgb(255, 255, 255),
      .opacity = 255,
      .size_px = 144,
  };
  ret = bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_digits_144,
                                      "42", 142, 180, &digits));

  loom_text_style_t label = {
      .color = loom_rgb(168, 190, 205),
      .opacity = 255,
      .size_px = 16,
  };
  ret = bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_16, "KM/H",
                                      218, 320, &label));

  for (int i = 0; i < 3; ++i) {
    loom_rect_t panel = loom_rect(24 + i * 150, 536, 132, 78);
    loom_linear_gradient_t fill = {
        .p0 = {panel.x, panel.y},
        .p1 = {panel.x + panel.w, panel.y + panel.h},
        .color0 = loom_rgba(18, 27, 33, 232),
        .color1 = loom_rgba(104, 202, 255, 52),
    };
    ret = bench_ret(ret,
                    loom_fill_round_rect_linear_gradient(gfx, panel, 14,
                                                         &fill));
    ret = bench_ret(ret, loom_draw_text(gfx, &loom_font_noto_sans_16,
                                        i == 0 ? "MOTOR" : i == 1 ? "CTRL"
                                                                  : "POWER",
                                        panel.x + 14, panel.y + 10, &label));
  }

  return ret;
}

static const loom_bench_case_t s_cases[] = {
    {"clear", bench_clear},
    {"fill_rect", bench_fill_rect},
    {"fill_rect_alpha", bench_fill_rect_alpha},
    {"fill_rect_linear_gradient", bench_fill_rect_linear_gradient},
    {"stroke_rect", bench_stroke_rect},
    {"fill_round_rect", bench_fill_round_rect},
    {"fill_round_rect_linear_gradient", bench_fill_round_rect_gradient},
    {"stroke_round_rect", bench_stroke_round_rect},
    {"fill_circle", bench_fill_circle},
    {"fill_circle_radial_gradient", bench_fill_circle_radial_gradient},
    {"stroke_circle", bench_stroke_circle},
    {"line", bench_line},
    {"arc", bench_arc},
    {"arc_gradient_sweep", bench_arc_gradient_sweep},
    {"arc_gradient_radial", bench_arc_gradient_radial},
    {"bitmap_rgb888", bench_bitmap_rgb888},
    {"bitmap_rgba8888", bench_bitmap_rgba8888},
    {"bitmap_a8", bench_bitmap_a8},
    {"text_16", bench_text_16},
    {"text_32", bench_text_32},
    {"text_digits_96", bench_text_digits_96},
    {"text_digits_144", bench_text_digits_144},
    {"clip_stack", bench_clip_stack},
    {"mixed_scene", bench_mixed_scene},
};

static loom_err_t bench_draw_frame(loom_t *gfx, const loom_bench_case_t *bench,
                                   int frame) {
  loom_err_t ret = loom_begin_frame(gfx);
  if (ret != LOOM_OK) {
    return ret;
  }

  ret = bench->draw(gfx, frame);
  loom_err_t end_ret = loom_end_frame(gfx);
  return ret == LOOM_OK ? end_ret : ret;
}

static esp_err_t bench_run_case(loom_t *gfx, const loom_bench_case_t *bench,
                                size_t index) {
  for (int i = 0; i < CONFIG_PEAK_LOOM_BENCHMARK_WARMUP_FRAMES; ++i) {
    loom_err_t ret = bench_draw_frame(gfx, bench, -i - 1);
    if (ret != LOOM_OK) {
      ESP_LOGE(TAG, "%s warmup failed: %d", bench->name, (int)ret);
      return loom_err_to_esp_err(ret);
    }
  }

  loom_bench_stats_t stats = {
      .min_us = INT64_MAX,
  };

  for (int sample = 0; sample < CONFIG_PEAK_LOOM_BENCHMARK_SAMPLES; ++sample) {
    int64_t start_us = esp_timer_get_time();
    for (int iteration = 0;
         iteration < CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS; ++iteration) {
      int frame = sample * CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS + iteration;
      loom_err_t ret = bench_draw_frame(gfx, bench, frame);
      if (ret != LOOM_OK) {
        ESP_LOGE(TAG, "%s sample failed: %d", bench->name, (int)ret);
        return loom_err_to_esp_err(ret);
      }
    }
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t frame_us = elapsed_us / CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS;
    if (frame_us < stats.min_us) {
      stats.min_us = frame_us;
    }
    if (frame_us > stats.max_us) {
      stats.max_us = frame_us;
    }
    stats.total_us += frame_us;
    stats.samples++;
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  int64_t avg_us = stats.samples > 0 ? stats.total_us / stats.samples : 0;
  uint32_t fps_x10 = avg_us > 0 ? (uint32_t)(10000000LL / avg_us) : 0;
  ESP_LOGI(TAG,
           "%02u %-32s frames=%u warmup=%u min=%lld us avg=%lld us max=%lld "
           "us avg_fps=%u.%u",
           (unsigned)index, bench->name,
           (unsigned)(CONFIG_PEAK_LOOM_BENCHMARK_SAMPLES *
                      CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS),
           (unsigned)CONFIG_PEAK_LOOM_BENCHMARK_WARMUP_FRAMES,
           (long long)stats.min_us, (long long)avg_us,
           (long long)stats.max_us, (unsigned)(fps_x10 / 10),
           (unsigned)(fps_x10 % 10));

  return ESP_OK;
}

esp_err_t loom_benchmark_run(esp_lcd_panel_handle_t panel) {
  if (panel == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(bench_init_bitmaps(), TAG, "allocate benchmark bitmaps");

  loom_esp_idf_config_t cfg = {
      .width = BENCH_DISPLAY_WIDTH,
      .height = BENCH_DISPLAY_HEIGHT,
      .format = LOOM_PIXEL_FORMAT_RGB888,
      .tile_height = BENCH_TILE_HEIGHT,
      .buffer_count = BENCH_BUFFER_COUNT,
      .command_capacity = BENCH_COMMAND_CAPACITY,
      .panel = panel,
  };

  loom_esp_idf_t *backend = NULL;
  loom_t *gfx = NULL;
  esp_err_t ret = loom_esp_idf_create(&cfg, &backend, &gfx);
  if (ret != ESP_OK) {
    bench_free_bitmaps();
    ESP_LOGE(TAG, "create benchmark renderer failed: %s",
             esp_err_to_name(ret));
    return ret;
  }

  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT);
  size_t largest_internal = heap_caps_get_largest_free_block(
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  ESP_LOGI(TAG,
           "starting Loom display benchmark: cases=%u samples=%u "
           "iterations=%u warmup=%u free_internal=%u largest_internal=%u",
           (unsigned)(sizeof(s_cases) / sizeof(s_cases[0])),
           (unsigned)CONFIG_PEAK_LOOM_BENCHMARK_SAMPLES,
           (unsigned)CONFIG_PEAK_LOOM_BENCHMARK_ITERATIONS,
           (unsigned)CONFIG_PEAK_LOOM_BENCHMARK_WARMUP_FRAMES,
           (unsigned)free_internal, (unsigned)largest_internal);

  ret = ESP_OK;
  for (size_t i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); ++i) {
    ret = bench_run_case(gfx, &s_cases[i], i);
    if (ret != ESP_OK) {
      break;
    }
  }

  loom_esp_idf_destroy(backend);
  bench_free_bitmaps();
  ESP_LOGI(TAG, "Loom display benchmark finished: %s", esp_err_to_name(ret));
  return ret;
}

#else

esp_err_t loom_benchmark_run(esp_lcd_panel_handle_t panel) {
  (void)panel;
  return ESP_OK;
}

#endif
