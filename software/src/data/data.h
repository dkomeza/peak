#pragma once

#include <Arduino.h>
#include <atomic>
#include "controller.h"

namespace data
{
    // Type aliases for better readability
    using AtomicByte = std::atomic<uint8_t>;
    using AtomicInt16 = std::atomic<int16_t>;
    using AtomicUInt16 = std::atomic<uint16_t>;
    using AtomicFloat = std::atomic<float>;
    using AtomicSupport = std::atomic<cycleiq_support_mode_t>;
    using AtomicRide = std::atomic<cycleiq_ride_mode_t>;

    /// Global variables for data sharing across tasks

    // Home page data
    inline AtomicByte batteryLevel{69};                   // Battery level in percentage (0-100)
    inline AtomicFloat batteryVoltage{54.0f};             // Battery voltage in volts
    inline AtomicInt16 motorTemperature{0};               // Motor temperature in degrees Celsius
    inline AtomicFloat speed{0.0f};                       // Speed in km/h
    inline AtomicUInt16 power{0};                         // Power in watts
    inline AtomicByte gear{0};                            // Current gear (0-5, 0 = neutral)
    inline AtomicSupport supportMode{CYCLEIQ_MODE_PAS};   // Default to PAS mode
    inline AtomicRide rideMode{CYCLEIQ_RIDE_MODE_NORMAL}; // Default to NORMAL mode
    inline AtomicFloat tripDistance{0.0f};                // Trip distance in km
    inline AtomicUInt16 range{0};                         // Estimated range in km (rounded)
    
    void init(void);
}
