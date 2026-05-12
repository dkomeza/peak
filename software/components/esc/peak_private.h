#ifndef PEAK_PRIVATE_H
#define PEAK_PRIVATE_H

#include <stdint.h>

static const uint8_t PEAK_CAN_ID = 0x45;

typedef enum {
  PEAK_PACKET_TYPE_HOME_MAIN = 0x01,
  PEAK_PACKET_TYPE_HOME_SECONDARY = 0x02,
} peak_packet_type_t;

#endif
