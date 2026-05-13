#ifndef ESC_PEAK_H
#define ESC_PEAK_H

#include <stdint.h>

typedef enum {
  CYCLEIQ_MODE_PAS = 0,
  CYCLEIQ_MODE_TORQUE,
  CYCLEIQ_MODE_HYBRID,
} cycleiq_support_mode_t;

typedef enum {
  CYCLEIQ_RIDE_MODE_NORMAL = 0,
  CYCLEIQ_RIDE_MODE_MOUNTAIN,
} cycleiq_ride_mode_t;

typedef struct {
  // Battery data
  uint8_t battery_percentage;
  float battery_voltage;
  float battery_current;
  float watt_hours;
  float amp_hours;

  // Motor data
  int8_t motor_temperature;
  int8_t controller_temperature;
  float motor_current;
  uint16_t motor_rpm;

  // Controller data
  uint8_t assist_level;
  cycleiq_support_mode_t support_mode;
  cycleiq_ride_mode_t ride_mode;

  // Live data
  float speed;
  uint16_t power;

  // Trip data
  float trip_distance;
  float trip_time;
  float trip_average_speed;
  uint8_t trip_estimated_range;
} esc_peak_data_t;

/**
 * Initializes the ESC Peak module.
 */
void esc_peak_init(void);

/**
 * Gets the latest data received from the ESC.
 * This is thread safe and blocking.
 */
void esc_peak_get_data(esc_peak_data_t *data);

#endif
