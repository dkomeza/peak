#pragma once

#include <Arduino.h>

namespace CRC16
{
    /**
     * @brief Calculate the CRC16 checksum of the given data.
     * @param data Pointer to the data buffer.
     * @param length Length of the data buffer.
     * @return The calculated CRC16 checksum.
     */
    uint16_t calc(const uint8_t *data, size_t length);

    /**
     * @brief Add data to the CRC calculation.
     * @param crc Current CRC value.
     * @param data Pointer to the data buffer.
     * @param length Length of the data buffer.
     * @return Updated CRC value after adding the data.
     */
    uint16_t add(uint16_t crc, const uint8_t *data, size_t length);
    uint16_t add(uint16_t crc, uint8_t data);
} // namespace CRC16