#pragma once

#include <Arduino.h>

#define PEAK_CAN_ID 0x6Au

namespace CAN
{
    void setup();

    bool sendMessage(uint32_t id, const uint8_t *data, size_t length, bool extended = false);

    void registerCallback(uint32_t id, uint32_t mask, void (*callback)(uint32_t id, const uint8_t *data, size_t length));
    void unregisterCallback(uint32_t id);

    extern String canDebug; // Debug string for CAN messages
}