#ifndef ESC_KT_H
#define ESC_KT_H

typedef struct {
  float temperature;
  float current;
  float voltage;
} esc_kt_data_t;

/**
 * Initializes the ESC KT module.
 * Starts both the the receive and send tasks */
void esc_kt_init(void);

/**
 * Gets the latest data received from the ESC.
 * This is thread safe and blocking.
 */
esc_kt_data_t *esc_kt_get_data(void);

#endif
