#ifndef COMPONENTS_BACKGROUND_H
#define COMPONENTS_BACKGROUND_H

#include "lvgl.h"

typedef struct {
  lv_obj_t *root;
  lv_obj_t *glow;
} background_view_t;

background_view_t background_create(lv_obj_t *parent, lv_coord_t width,
                                    lv_coord_t height);

#endif
