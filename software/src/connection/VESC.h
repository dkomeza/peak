#pragma once

#include <Arduino.h>

#define MAX_BUFFER_SIZE 512 // Same as the maximum size of the VESC RX buffer

namespace VESC
{
  extern QueueHandle_t packetQueue; // Queue to hold outgoing packets
  // extern PACKET_STATE_t rxPacket;
  // extern PACKET_STATE_t txPacket;

  struct TxPacket
  {
    size_t length; // Length of the packet data
    uint8_t data[MAX_BUFFER_SIZE]; // Buffer to hold the packet data
  };

  void setup();
  void handleIncomingData(uint8_t data);

  void createPacket(const uint8_t* data, size_t length);
  void sendData(uint32_t id, uint8_t* data, uint16_t length);
  void receiveData(uint32_t id, const uint8_t* source, size_t length);
}