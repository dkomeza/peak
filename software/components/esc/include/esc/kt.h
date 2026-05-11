#ifndef ESC_KT_H
#define ESC_KT_H

#include <stdint.h>

typedef struct {
  uint8_t val;
  char *name;
} wheel_size_t;

static const wheel_size_t WHEEL_SIZES[] = {
    {30, "29\""}, {28, "28\""}, {24, "700C"},
    {20, "26\""}, {16, "24\""}, {8, "20\""},
};

typedef struct {
  uint8_t battery_level;
  float speed;
  uint16_t power;
  int8_t motor_temp;

  bool throttle;
  bool cruise;
  bool assist;
  bool brake;
} esc_kt_data_t;

typedef struct {
  uint8_t assist_level;
  bool light;

  // Basic settings
  uint8_t max_speed;
  wheel_size_t wheel_size;

  // P params
  uint8_t P1;
  uint8_t P2;
  uint8_t P3;
  uint8_t P4;
  uint8_t P5;

  // C params
  uint8_t C1;
  uint8_t C2;
  uint8_t C4;
  uint8_t C5;
  uint8_t C12;
  uint8_t C13;
  uint8_t C14;
} peak_kt_data_t;

/**
 * Initializes the ESC KT module.
 * Starts both the the receive and send tasks */
void esc_kt_init(void);

/**
 * Gets the latest data received from the ESC.
 * This is thread safe and blocking.
 * Remember to free the returned pointer when done!!!
 */
esc_kt_data_t *esc_kt_get_data(void);

#endif
