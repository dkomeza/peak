#ifndef COMPONENTS_CARD_H
#define COMPONENTS_CARD_H

#include "lvgl.h"

lv_obj_t *glass_card_create(lv_obj_t *parent);

typedef struct {
  const char *label;
  const char *icon;
  lv_subject_t *value;
  const char *value_format;
  lv_subject_t *status;
} metric_card_props_t;

lv_obj_t *metric_card_create(lv_obj_t *parent,
                             const metric_card_props_t *props);

#endif
