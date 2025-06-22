#pragma once

#include <Arduino.h>
#include <atomic>

namespace data
{
    // Support modes
    enum class SupportMode
    {
        PAS,   // Pedal Assist System
        TORQUE // Torque mode
    };

    // Ride modes
    enum class RideMode
    {
        NORMAL,  // City riding mode (25 km/h, 250 W)
        MOUNTAIN // Mountain riding mode (unlimited)
    };

    // Type aliases for better readability
    using AtomicByte = std::atomic<uint8_t>;
    using AtomicInt16 = std::atomic<int16_t>;
    using AtomicUInt16 = std::atomic<uint16_t>;
    using AtomicFloat = std::atomic<float>;
    using AtomicSupport = std::atomic<SupportMode>;
    using AtomicRide = std::atomic<RideMode>;

    /// Global variables for data sharing across tasks

    // Home page data
    inline AtomicByte batteryLevel{69};                 // Battery level in percentage (0-100)
    inline AtomicFloat batteryVoltage{54.0f};           // Battery voltage in volts
    inline AtomicInt16 motorTemperature{0};             // Motor temperature in degrees Celsius
    inline AtomicFloat speed{0.0f};                     // Speed in km/h
    inline AtomicUInt16 power{0};                       // Power in watts
    inline AtomicByte gear{0};                          // Current gear (0-5, 0 = neutral)
    inline AtomicSupport supportMode{SupportMode::PAS}; // Default to PAS mode
    inline AtomicRide rideMode{RideMode::NORMAL};       // Default to NORMAL mode
    inline AtomicFloat tripDistance{0.0f};              // Trip distance in km
    inline AtomicUInt16 range{0};                       // Estimated range in km (rounded)
}