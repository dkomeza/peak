#include "VESC.h"
#include "CAN.h"
#include "datatypes.h"
#include "utils/crc.h"
#include "BLE.h"

#include "VESCPacket.h"

#define CONTROLLER_ID 69
#define VESC_TOOL_ID 254
#define MAX_BUFFER_SIZE 512 // Same as the maximum size of the VESC RX buffer
#define MAX_BUFFERS 3

typedef void (*tx_func_t)(uint8_t* data, size_t len);

uint8_t rxBuffer[MAX_BUFFERS][MAX_BUFFER_SIZE] = { 0 }; // Buffer for incoming data
int rxBuffers[MAX_BUFFERS] = { 0 };                     // Array to hold indices of RX buffers

PacketDecoder packetDecoder; // Packet decoder instance
tx_func_t tx_func = nullptr;
uint8_t packet[MAX_BUFFER_SIZE] = { 0 }; // Buffer for outgoing packets

void packValue(uint8_t* data, int& offset, uint32_t value)
{
    data[offset++] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[offset++] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset++] = static_cast<uint8_t>(value & 0xFF);
}

void packValue(uint8_t* data, int& offset, uint16_t value)
{
    data[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[offset++] = static_cast<uint8_t>(value & 0xFF);
}

void VESC::createPacket(const uint8_t* data, size_t length)
{
    int packetLength = 0; // Initialize packet length
    packetLength = length <= 255 ? length + 2 : length <= 65535 ? length + 3
        : length + 4;
    packetLength += 3; // Add 3 for the end of the packet (CRC1, CRC2, and end byte)

    memset(packet, 0, packetLength); // Clear the packet buffer

    int s_ind = 0;
    if (length <= 255)
    {
        packet[s_ind++] = 2;      // Command ID for short data
        packet[s_ind++] = length; // Length of the data
    }
    else if (length <= 65535)
    {
        packet[s_ind++] = 3;                    // Command ID for medium data
        packet[s_ind++] = (length >> 8) & 0xFF; // High byte of length
        packet[s_ind++] = length & 0xFF;        // Low byte of length
    }
    else
    {
        packet[s_ind++] = 4;                     // Command ID for long data
        packet[s_ind++] = (length >> 16) & 0xFF; // Byte 2 of length
        packet[s_ind++] = (length >> 8) & 0xFF;  // Byte 1 of length
        packet[s_ind++] = length & 0xFF;         // Byte 0 of length
    }

    memcpy(packet + s_ind, data, length);     // Copy the data into the send buffer
    s_ind += length;                          // Update the index to the end of the data
    uint16_t crc = CRC16::calc(data, length); // Calculate CRC for the data
    packValue(packet, s_ind, crc);            // Pack the CRC into the send buffer
    packet[s_ind++] = 0x03;                   // End byte to indicate the end of the packet

    if (tx_func)
    {
        tx_func(packet, s_ind); // Call the transmission function with the packet
    }
}

bool sendPacket(uint32_t id, CAN_PACKET_ID packetId, const uint8_t* data, size_t length, bool extended = false)
{
    uint32_t fullId = CONTROLLER_ID | (static_cast<uint32_t>(packetId) << 8);

    if (length > 8)
        return false; // Return false if data length exceeds maximum CAN frame size

    return CAN::sendMessage(fullId, data, length, extended);
}

void VESC::setTxCallback(void (*send_func)(uint8_t* data, size_t len))
{
    if (send_func) {
        tx_func = send_func;
    }
}

void onPacketReceived(const uint8_t* data, size_t length)
{
    VESC::sendData(VESC_TOOL_ID, (uint8_t*)data, length);
}

void VESC::setup()
{
    packetDecoder.onPacket(onPacketReceived); // Register the packet handler

    CAN::registerCallback(VESC_TOOL_ID, 0xFF, VESC::receiveData);
}

void VESC::handleIncomingData(uint8_t data)
{
    packetDecoder.feed(data);
}

void VESC::sendData(uint32_t id, uint8_t* data, uint16_t length)
{
    if (length < 1)
        return; // Return false if data length is too short

    // Serial.printf("[ESP=>VESC] ");
    // for (int i = 0; i < length; i++)
    // {
    //     Serial.printf("%02X ", data[i]); // Print each byte of the data
    // }
    // Serial.println(""); // New line after printing the data

    uint8_t send_buffer[8] = { 0 };

    if (length <= 6)
    {
        int ind = 0;
        send_buffer[ind++] = id; // Set the first byte to the sender ID
        send_buffer[ind++] = 0;
        memccpy(send_buffer + ind, data, 0, length); // Copy the data into the send buffer
        ind += length;                               // Update the index to the end of the data

        sendPacket(CONTROLLER_ID, CAN_PACKET_ID::CAN_PACKET_PROCESS_SHORT_BUFFER, send_buffer, ind, true);
    }
    else
    {
        int end_a = 0;
        for (int i = 0; i < length; i += 7)
        {
            int offset = i;
            if (i > 255)
                break;

            end_a = offset + 7;

            int ind = 0;
            send_buffer[ind++] = i;

            while (ind < 8 && offset < length)
            {
                send_buffer[ind++] = data[offset++];
            }
            sendPacket(CONTROLLER_ID, CAN_PACKET_ID::CAN_PACKET_FILL_RX_BUFFER, send_buffer, ind, true);
        }

        for (uint16_t i = end_a; i < length; i += 6)
        {
            int offset = i;
            int ind = 0;
            packValue(send_buffer, ind, i); // Pack the offset

            while (ind < 8 && offset < length)
            {
                send_buffer[ind++] = data[offset++];
            }
            sendPacket(CONTROLLER_ID, CAN_PACKET_ID::CAN_PACKET_FILL_RX_BUFFER_LONG, send_buffer, ind, true);
        }

        int ind = 0;
        send_buffer[ind++] = id;
        send_buffer[ind++] = 0;              // Reserved byte
        packValue(send_buffer, ind, length); // Pack the total length of the data
        packValue(send_buffer, ind, CRC16::calc(data, length));
        sendPacket(CONTROLLER_ID, CAN_PACKET_ID::CAN_PACKET_PROCESS_RX_BUFFER, send_buffer, ind, true);
    }
}

void VESC::receiveData(uint32_t id, const uint8_t* source, size_t length)
{
    CAN_PACKET_ID cmd = static_cast<CAN_PACKET_ID>(id >> 8); // Extract the command from the ID
    // Serial.printf("[VESC=>ESP] ");
    // for (size_t i = 0; i < length; i++)
    // {
    //     Serial.printf("%02X ", source[i]); // Print each byte of the received data
    // }
    // Serial.println(""); // New line after printing the data

    switch (cmd)
    {
    case CAN_PACKET_ID::CAN_PACKET_FILL_RX_BUFFER:
    {
        int offset = source[0];
        source++;
        length--;

        int buf_ind = -1;
        for (int i = 0; i < MAX_BUFFERS; i++)
        {
            if (rxBuffers[i] == offset)
            {
                buf_ind = i; // Find the buffer index that matches the offset
                break;
            }
        }

        if (buf_ind < 0)
        {
            if (offset == 0)
            {
                buf_ind = 0;
            }
            else
            {
                break;
            }
        }

        if (rxBuffers[buf_ind] + length > MAX_BUFFER_SIZE)
            break;

        memcpy(rxBuffer[buf_ind] + rxBuffers[buf_ind], source, length); // Copy the data into the RX buffer
        rxBuffers[buf_ind] += length;                                   // Update the RX buffer index
    }
    break;
    case CAN_PACKET_ID::CAN_PACKET_FILL_RX_BUFFER_LONG:
    {
        int offset = (int)source[0] << 8 | (int)source[1]; // Combine the first two bytes to get the offset
        source += 2;                                       // Move the pointer past the offset bytes
        length -= 2;                                       // Adjust the length to account for the offset bytes

        int buf_ind = -1;
        for (int i = 0; i < MAX_BUFFERS; i++)
        {
            if (rxBuffers[i] == offset)
            {
                buf_ind = i; // Find the buffer index that matches the offset
                break;
            }
        }

        if (buf_ind < 0)
        {
            if (offset == 0)
            {
                buf_ind = 0;
            }
            else
            {
                break;
            }
        }

        memcpy(rxBuffer[buf_ind] + rxBuffers[buf_ind], source, length); // Copy the data into the RX buffer
        rxBuffers[buf_ind] += length;                                   // Update the RX buffer index
    }
    break;
    case CAN_PACKET_ID::CAN_PACKET_PROCESS_RX_BUFFER:
    {
        int expectedLength = source[2] << 8 | source[3]; // Combine the third and fourth bytes to get the expected length
        uint16_t crc = source[4] << 8 | source[5];       // Combine the fifth and sixth bytes to get the CRC

        int buf_ind = -1;
        for (int i = 0; i < MAX_BUFFERS; i++)
        {
            if (rxBuffers[i] == expectedLength)
            {
                buf_ind = i; // Find the buffer index that matches the expected length
                break;
            }
        }

        if (buf_ind < 0)
        {
            // Clear all RX buffers if no matching buffer is found
            for (int i = 0; i < MAX_BUFFERS; i++)
            {
                rxBuffers[i] = 0;                        // Reset the RX buffer index
                memset(rxBuffer[i], 0, MAX_BUFFER_SIZE); // Clear the RX buffer
            }
            break; // If no buffer matches the expected length, exit the case
        }

        if (CRC16::calc(reinterpret_cast<const uint8_t*>(rxBuffer[buf_ind]), rxBuffers[buf_ind]) != crc)
        {
            memset(rxBuffer[buf_ind], 0, MAX_BUFFER_SIZE); // Clear the RX buffer if CRC does not match
            rxBuffers[buf_ind] = 0;                        // Reset the RX buffer index
            break;
        }

        VESC::createPacket(rxBuffer[buf_ind], rxBuffers[buf_ind]); // Create a packet from the RX buffer
        rxBuffers[buf_ind] = 0;                        // Reset the RX buffer index after sending
        memset(rxBuffer[buf_ind], 0, MAX_BUFFER_SIZE); // Clear the RX buffer after sending
    }
    break;
    case CAN_PACKET_ID::CAN_PACKET_PROCESS_SHORT_BUFFER:
    {
        VESC::createPacket(source + 2, length - 2); // Create a packet from the short buffer data
    }
    break;
    default:
        break;
    }
}

// #include <SPIFFS.h>

// #include "utils/crc.h"


// extern "C" {
// #include "heatshrink_encoder.h"
// }

// #define CONTROLLER_ID 69



// class Request; // Forward declaration for the Request class

// #define MAX_REQUESTS 10
// Request* requests[MAX_REQUESTS];

// class Request
// {
// public:
//     uint8_t request_id;
//     struct Response
//     {
//         uint8_t id;
//         uint8_t* data;
//         size_t length;
//     };

//     Request(uint8_t id, uint8_t* data, size_t length)
//         : request_id(id), request_data(data), request_length(length) {
//         bool found = false;
//         for (int i = 0; i < MAX_REQUESTS; i++)
//         {
//             if (!requests[i]) // Find an empty slot
//             {
//                 requests[i] = this;
//                 found = true;
//                 request_index = i; // Store the index of the request
//                 break;
//             }
//         }

//         if (!found) {
//             response_received = true;
//             response.id = 0; // Set to 0 if no slot found
//             response.data = nullptr; // No data if no slot found
//             response.length = 0; // No length if no slot found
//             return;
//         }

//         VESC::sendData(VESC::VESC_TOOL_ID, request_data, request_length); // Send the request data
//     }

//     ~Request() {
//         if (response_data)
//             delete[] response_data; // Free the allocated memory for response data

//         if (request_index >= 0 && request_index < MAX_REQUESTS)
//             requests[request_index] = nullptr; // Clear the request from the requests array
//     }

//     void createResponse(const uint8_t* data, size_t length) {
//         response_id = data[0]; // First byte is the response ID
//         response_data = new uint8_t[length - 1]; // Allocate memory for the response data
//         memcpy(response_data, data + 1, length - 1); // Copy the response data
//         response_length = length - 1; // Set the response length

//         response.id = response_id;
//         response.data = response_data;
//         response.length = response_length;

//         response_received = true; // Mark that a response has been received
//     }

//     Response await(int timeout = 1000) {
//         unsigned long start_time = millis();
//         while (!response_received && (millis() - start_time < timeout))
//         {
//             delay(1); // Small delay to avoid busy waiting
//         }

//         if (response_received)
//         {
//             return response;
//         }
//         else
//         {
//             return { 0, nullptr, 0 }; // Return an empty response if timeout occurs
//         }
//     }

// private:
//     uint8_t* request_data;
//     size_t request_length;

//     int request_index; // Index of the request in the requests array

//     uint8_t response_id = 0;
//     uint8_t* response_data = nullptr; // Pointer to hold the response data
//     size_t response_length = 0; // Length of the response data

//     bool response_received = false;
//     Response response = { 0, nullptr, 0 }; // Response structure to hold the response data
// };




// void handlePacket(const uint8_t* data, size_t length)
// {
//     if (length < 1)
//         return; // Return if data length is too short

//     uint8_t id = data[0];

//     for (int i = 0; i < MAX_REQUESTS; i++)
//     {
//         if (requests[i] && requests[i]->request_id == id) // Find the request with the matching ID
//         {
//             requests[i]->createResponse(data, length); // Create a response with the remaining data
//             return; // Exit after processing the request
//         }
//     }
// }





// bool ping(int timeout = 5000)
// {
//     static const int PING_ATTEMPTS = 5; // Number of attempts to send the ping - this makes sure that the connection is stable
//     uint8_t data[1] = { VESC::VESC_TOOL_ID }; // Data for the ping message

//     BLE::print("Sending ping to VESC... ");
//     for (int i = 0; i < PING_ATTEMPTS; i++) {
//         auto request = Request(static_cast<uint8_t>(CONTROLLER_ID), data, sizeof(data));
//         Request::Response response = request.await(timeout);

//         if (response.length == 1 && response.id == static_cast<uint8_t>(CONTROLLER_ID) &&
//             response.data[0] == static_cast<uint8_t>(HW_TYPE::HW_TYPE_VESC)) // Check if the response indicates success
//         {
//             BLE::printf("%d ", i + 1);
//         }
//         else {
//             return false; // Return false if ping failed
//         }
//         delay(100); // Wait a short period before the next ping attempt
//     }

//     BLE::println("Done.");
//     return true;
// }

// bool eraseMemory(uint32_t size, int timeout = 5000)
// {
//     uint8_t data[5] = { 0 };
//     int ind = 0;
//     data[ind++] = static_cast<uint8_t>(COMM_PACKET_ID::COMM_ERASE_NEW_APP);
//     packValue(data, ind, size); // Pack the size of the memory to erase

//     BLE::println("Erasing memory...");
//     auto request = Request(static_cast<uint8_t>(COMM_PACKET_ID::COMM_ERASE_NEW_APP), data, ind);
//     Request::Response response = request.await(timeout);

//     if (response.length >= 1 && response.id == static_cast<uint8_t>(COMM_PACKET_ID::COMM_ERASE_NEW_APP) &&
//         response.data[0] == 0x01) // Check if the response indicates success
//     {
//         BLE::println("Memory erased successfully.");
//         return true; // Return true if memory erase command was successful
//     }

//     return false; // Return false if memory erase command failed
// };

// bool flashFirmware(File* firmwareFile, uint32_t file_size)
// {
//     const int chunkSize = 384;
//     uint8_t data[chunkSize + 5];

//     uint32_t offset = 0;
//     float progress = 0.0f;
//     int ind = 0;

//     unsigned long notificationTime = 0;
//     int notificationInterval = 500;

//     while (offset < file_size)
//     {
//         // Prepare the data packet
//         ind = 0;
//         data[ind++] = static_cast<uint8_t>(COMM_PACKET_ID::COMM_WRITE_NEW_APP_DATA);
//         packValue(data, ind, offset); // Pack the current offset

//         int sz = (file_size - offset > chunkSize) ? chunkSize : (file_size - offset);

//         int bytesRead = firmwareFile->read(data + ind, sz);
//         if (bytesRead <= 0)
//         {
//             BLE::println("Error reading firmware file.");
//             return false; // Return false if reading the file fails
//         }

//         bool hasData = false;
//         for (int i = 0; i < bytesRead; i++)
//         {
//             if (data[ind + i] != 0xFF)
//             {
//                 hasData = true;
//                 break;
//             }
//         }

//         if (!hasData)
//             continue;

//         auto response = Request(static_cast<uint8_t>(COMM_PACKET_ID::COMM_WRITE_NEW_APP_DATA), data, bytesRead + ind).await(1000);

//         if (response.length < 1 || response.id != static_cast<uint8_t>(COMM_PACKET_ID::COMM_WRITE_NEW_APP_DATA) ||
//             response.data[0] != 0x01) // Check if the response indicates success
//         {
//             BLE::println("Failed to flash firmware chunk at offset " + String(offset));
//             return false; // Return false if the flash command failed
//         }

//         offset += sz;                                        // Update the offset by the number of bytes read
//         progress = static_cast<float>(offset) / file_size * 100.0f; // Calculate the progress percentage
//         if (progress > 100.0f)
//             progress = 100.0f; // Ensure progress does not exceed 100%

//         if (millis() - notificationTime >= notificationInterval) {
//             BLE::printf("Flashing progress: %.2f%%\n", progress); // Print the flashing progress
//             notificationTime = millis(); // Update the notification time
//         }
//         delay(1); // Wait for a short period before sending the next chunk
//     }

//     return true;
// };

// #define READ_SIZE 256
// #define COMPRESSED_BUFFER_SIZE 512

// static heatshrink_encoder hse;
// uint8_t hsRxBuffer[READ_SIZE];
// uint8_t compressedBuffer[COMPRESSED_BUFFER_SIZE];

// void compressFirmware(File& input, const char* outputPath) {
//     File output = SPIFFS.open(outputPath, "w");

//     if (!output) {
//         Serial.println("Failed to open output file");
//         return;
//     }

//     while (true) {
//         heatshrink_encoder_reset(&hse);

//         size_t   count = 0;
//         uint32_t sunk = 0;
//         uint32_t polled = 0;
//         size_t bytesRead = input.read(hsRxBuffer, READ_SIZE);
//         size_t output_size = COMPRESSED_BUFFER_SIZE;
//         if (bytesRead == 0) {
//             break;
//         }

//         while (sunk < bytesRead) {
//             heatshrink_encoder_sink(&hse, &hsRxBuffer[sunk], bytesRead - sunk, &count);
//             sunk += count;
//             if (sunk == bytesRead) {
//                 heatshrink_encoder_finish(&hse);
//             }

//             HSE_poll_res pres;
//             do {
//                 pres = heatshrink_encoder_poll(&hse,
//                     &compressedBuffer[polled],
//                     output_size - polled,
//                     &count);
//                 polled += count;
//             } while (pres == HSER_POLL_MORE);
//             if (sunk == bytesRead) {
//                 heatshrink_encoder_finish(&hse);
//             }
//         }

//         if (polled > 0) {
//             output.write(compressedBuffer, polled); // Write the compressed data to the output file
//         }
//     }

//     output.close(); // Close the output file
// }
// void prependHeaderToFile(File& originalFile, File& newFile, const uint8_t* header, size_t headerLength) {
//     // Step 1: Write the 6-byte header
//     newFile.write(header, headerLength);

//     // Step 2: Copy original file content
//     uint8_t buffer[256];
//     size_t bytesRead;
//     while ((bytesRead = originalFile.read(buffer, sizeof(buffer))) > 0) {
//         newFile.write(buffer, bytesRead);
//     }
// }

// void VESC::flashFirmware()
// {
//     String fileName = "/vesc.bin"; // Path to the firmware file in SPIFFS

//     // Check if the file exists
//     if (!SPIFFS.exists(fileName))
//     {
//         BLE::println("Firmware file not found: " + fileName);
//         return;
//     }

//     File firmwareFile = SPIFFS.open(fileName, "r");
//     if (!firmwareFile)
//     {
//         BLE::println("Failed to open firmware file: " + fileName);
//         return;
//     }
//     uint32_t fileSize = firmwareFile.size(); // Get the size of the firmware file
//     if (fileSize == 0)
//     {
//         BLE::println("Firmware file is empty: " + fileName);
//         firmwareFile.close();
//         return;
//     }
//     uint32_t firmwareSize = firmwareFile.read() << 24 | firmwareFile.read() << 16 |
//         firmwareFile.read() << 8 | firmwareFile.read(); // Read the file size from the first 4 bytes

//     CAN::registerCallback(VESC::VESC_TOOL_ID, 0xFF, VESC::receiveData);

//     BLE::printf("Flashing firmware from %s (%zu bytes)\n", fileName.c_str(), fileSize);
//     delay(100); // Give some time before starting the flashing process
//     if (!ping(1000))
//     {
//         BLE::println("Ping failed, cannot flash firmware.");
//         firmwareFile.close();
//         return;
//     }
//     delay(100);

//     // Erase the memory where the firmware will be written
//     if (!eraseMemory(firmwareSize, 10000))
//     {
//         BLE::println("Failed to erase memory for firmware flash.");
//         firmwareFile.close();
//         return;
//     }
//     delay(100);

//     if (!flashFirmware(&firmwareFile, fileSize - 4))
//     {
//         BLE::println("Failed to flash firmware.");
//         firmwareFile.close();
//         return;
//     }

//     BLE::println("Firmware flash completed successfully.");
//     firmwareFile.close();
//     delay(1000); // Wait for a second before rebooting

//     uint8_t bootloader_data[1] = { static_cast<uint8_t>(COMM_PACKET_ID::COMM_JUMP_TO_BOOTLOADER) };
//     BLE::println("Jumping to the bootloader...");
//     Request(static_cast<uint8_t>(COMM_PACKET_ID::COMM_JUMP_TO_BOOTLOADER), bootloader_data, 1).await(1000);
//     BLE::println("Flashing complete. The VESC should now reboot into the bootloader.");

//     // uint8_t rebootData[1] = { static_cast<uint8_t>(COMM_PACKET_ID::REBOOT) };
//     // sendPacket(CONTROLLER_ID, CAN_PACKET_ID::PROCESS_SHORT_BUFFER, rebootData, 1, true); // Send the reboot command
// }


