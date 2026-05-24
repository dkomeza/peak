#include "esc/controller.h"

const char *esc_controller_name(const esc_controller_t *controller) {
  if (controller == NULL || controller->ops == NULL) {
    return NULL;
  }

  return controller->ops->name;
}

esp_err_t esc_controller_set_power(const esc_controller_t *controller,
                                   bool enabled) {
  if (controller == NULL || controller->ops == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (controller->ops->set_power == NULL) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  return controller->ops->set_power(controller->ctx, enabled);
}

esp_err_t esc_controller_set_ride_mode(const esc_controller_t *controller,
                                       esc_ride_mode_t mode) {
  if (controller == NULL || controller->ops == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (controller->ops->set_ride_mode == NULL) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  return controller->ops->set_ride_mode(controller->ctx, mode);
}

esp_err_t esc_controller_set_gear(const esc_controller_t *controller,
                                  uint8_t gear) {
  if (controller == NULL || controller->ops == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (controller->ops->set_gear == NULL) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  return controller->ops->set_gear(controller->ctx, gear);
}

esp_err_t esc_controller_set_support_mode(const esc_controller_t *controller,
                                          esc_support_mode_t mode) {
  if (controller == NULL || controller->ops == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (controller->ops->set_support_mode == NULL) {
    return ESP_ERR_NOT_SUPPORTED;
  }

  return controller->ops->set_support_mode(controller->ctx, mode);
}
