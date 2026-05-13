#include "esc/peak.h"
#include "peak_private.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "connection/can.h"

static SemaphoreHandle_t esc_peak_data_mutex;

static esc_peak_data_t esc_peak_data = {0};

void esc_peak_parse_battery_data(uint32_t id, const uint8_t *data, uint8_t len,
                                 void *user_data) {
  if (len != 8)
    return;

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);

  esc_peak_data.battery_percentage = data[0] & 0x7F;
  esc_peak_data.amp_hours = data[7] / 10.0f;
  esc_peak_data.battery_voltage = ((data[2] << 8) | data[3]) / 100.0f;
  esc_peak_data.battery_current = ((data[4] << 8) | data[5]) / 100.0f;
  esc_peak_data.watt_hours = ((data[6] << 8) | data[7]) / 10.0f;

  xSemaphoreGive(esc_peak_data_mutex);
}

void esc_peak_parse_home_secondary(uint32_t id, const uint8_t *data,
                                   uint8_t len, void *user_data) {
  if (len != 8)
    return;

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);

  xSemaphoreGive(esc_peak_data_mutex);
}

void esc_peak_parse_home_main(uint32_t id, const uint8_t *data, uint8_t len,
                              void *user_data) {
  if (len != 8)
    return;

  xSemaphoreTake(esc_peak_data_mutex, portMAX_DELAY);

  xSemaphoreGive(esc_peak_data_mutex);
}

void esc_peak_init(void) {
  can_register_cb(PEAK_CAN_ID | PEAK_PACKET_TYPE_HOME_MAIN << 8, 0xFFFF,
                  esc_peak_parse_home_main, NULL);
  can_register_cb(PEAK_CAN_ID | PEAK_PACKET_TYPE_HOME_SECONDARY << 8, 0xFFFF,
                  esc_peak_parse_home_secondary, NULL);
}

void esc_peak_get_data(esc_peak_data_t *data) {}
