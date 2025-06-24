#pragma once

#include <Arduino.h>

namespace CAN
{
    void setup();

    // void sendMessage(uint32_t id, const uint8_t *data, size_t length);

    extern String canDebug; // Debug string for CAN messages
}