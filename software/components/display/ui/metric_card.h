#ifndef PEAK_DISPLAY_METRIC_CARD_H
#define PEAK_DISPLAY_METRIC_CARD_H

#include "lvgl.h"

typedef struct {
  lv_obj_t *value;
  lv_obj_t *detail;
} metric_card_t;

metric_card_t metric_card_create(lv_obj_t *parent, const char *title,
                                 lv_align_t align, int32_t x_offset,
                                 int32_t y_offset);

#endif
