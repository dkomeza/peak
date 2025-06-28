#pragma once

#include <Arduino.h>

void bleSend(const char* data, size_t len);

namespace BLE
{
    void setup();

    template <typename T>
    void print(const T& data)
    {
        String s = String(data);
        bleSend(s.c_str(), s.length());
    }

    template <typename T>
    void println(const T& data)
    {
        String s = String(data);
        s += "\r\n";
        bleSend(s.c_str(), s.length());
    }

    void printf(const char* format, ...);
} // namespace BLE
