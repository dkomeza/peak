#include "VESCPacket.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "utils/crc.h"
#include "BLE.h"

void PacketDecoder::onPacket(PacketCallback cb) {
    callback = cb;
}

void PacketDecoder::feed(uint8_t data) {
    processByte(data);
}
void PacketDecoder::feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        processByte(data[i]);
    }
}

void PacketDecoder::reset() {
    index = 0;
    expectedLength = 0;
}

void PacketDecoder::processByte(uint8_t byte) {
    if (index >= sizeof(buffer)) {
        reset(); // Reset if buffer overflow
        return;
    }

    buffer[index++] = byte;

    if (index == 1) {
        if (byte < 2 || byte > 4) {
            reset(); // Invalid header byte
            return;
        }
    }

    if (index == buffer[0]) {
        expectedLength = 0;
        for (size_t i = 1; i < index; i++) {
            expectedLength = (expectedLength << 8) | buffer[i];
        }
        if (expectedLength > MAX_DATA_SIZE) {
            reset(); // Invalid length
            return;
        }
    }

    size_t totalLen = buffer[0] + expectedLength + 3;

    if (index == totalLen) {
        uint8_t endByte = buffer[totalLen - 1];
        if (endByte != 0x03) {
            reset(); // Invalid end byte
            return;
        }

        uint16_t received_crc = (buffer[totalLen - 3] << 8) | buffer[totalLen - 2];
        uint16_t calculated_crc = CRC16::calc(buffer + buffer[0], expectedLength);

        if (received_crc == calculated_crc) {
            if (callback)
                callback(buffer + buffer[0], expectedLength);
        }

        reset();
    }
}