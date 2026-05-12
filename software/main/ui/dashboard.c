#include "ui/dashboard.h"

esp_err_t peak_dashboard_create(lv_obj_t *parent) {
  (void)parent;
  return ESP_OK;
}

void peak_dashboard_update(const peak_dashboard_data_t *data) { (void)data; }
