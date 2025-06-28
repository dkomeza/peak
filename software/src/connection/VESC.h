#pragma once

#include <Arduino.h>
#include "packet.h"

namespace VESC
{
  extern PACKET_STATE_t rxPacket;
  extern PACKET_STATE_t txPacket;

  void setup();
  void setTxCallback(void (*send_func)(uint8_t* data, size_t len) = nullptr);
  void handleIncomingData(uint8_t data);

  void createPacket(const uint8_t* data, size_t length);
  void sendData(uint32_t id, uint8_t* data, uint16_t length);
  void receiveData(uint32_t id, const uint8_t* source, size_t length);
}