#ifndef ESC_CONTROLLER_H
#define ESC_CONTROLLER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ESC_SUPPORT_MODE_PAS = 0,
  ESC_SUPPORT_MODE_TORQUE,
  ESC_SUPPORT_MODE_HYBRID,
} esc_support_mode_t;

typedef enum {
  ESC_RIDE_MODE_NORMAL = 0,
  ESC_RIDE_MODE_MOUNTAIN,
} esc_ride_mode_t;

typedef struct esc_controller_ops esc_controller_ops_t;

typedef struct {
  const esc_controller_ops_t *ops;
  void *ctx;
} esc_controller_t;

struct esc_controller_ops {
  const char *name;
  esp_err_t (*set_power)(void *ctx, bool enabled);
  esp_err_t (*set_ride_mode)(void *ctx, esc_ride_mode_t mode);
  esp_err_t (*set_gear)(void *ctx, uint8_t gear);
  esp_err_t (*set_support_mode)(void *ctx, esc_support_mode_t mode);
};

const char *esc_controller_name(const esc_controller_t *controller);
esp_err_t esc_controller_set_power(const esc_controller_t *controller,
                                   bool enabled);
esp_err_t esc_controller_set_ride_mode(const esc_controller_t *controller,
                                       esc_ride_mode_t mode);
esp_err_t esc_controller_set_gear(const esc_controller_t *controller,
                                  uint8_t gear);
esp_err_t esc_controller_set_support_mode(const esc_controller_t *controller,
                                          esc_support_mode_t mode);

#endif
