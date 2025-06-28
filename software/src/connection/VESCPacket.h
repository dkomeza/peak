#pragma once

#include <Arduino.h>

class PacketDecoder {
public:
    static constexpr size_t MAX_DATA_SIZE = 512;  // Adjust as needed
    using PacketCallback = void (*)(const uint8_t* data, size_t length);

    PacketDecoder() : index(0), expectedLength(0), callback(nullptr) {}

    void onPacket(PacketCallback cb);

    void feed(uint8_t data);
    void feed(const uint8_t* data, size_t len);

private:
    uint8_t buffer[MAX_DATA_SIZE + 8]; // Header + data + CRC + end
    size_t index = 0;
    uint8_t header_size = 0; // Size of the header (2, 3, or 4 bytes)
    size_t expectedLength = 0;
    PacketCallback callback = nullptr;

    void reset();
    void processByte(uint8_t byte);
};