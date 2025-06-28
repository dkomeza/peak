#include "CAN.h"
#include "datatypes.h"
#include "screen/screen.h"

#include <driver/twai.h>

#define PEAK_CAN_MASK 0x7Fu

#define CYCLEIQ_CAN_ID 0x6Bu

#define MAX_CAN_CALLBACKS 16

using namespace CAN;

String CAN::canDebug = ""; // Initialize the debug string for CAN messages

struct CAN_Callback
{
    uint32_t id; // CAN ID to match
    uint32_t mask;
    void (*callback)(uint32_t id, const uint8_t* data, size_t length); // Callback function to call when a message with this ID is received
};

CAN_Callback canCallbacks[MAX_CAN_CALLBACKS]; // Array to hold registered callbacks

void CANTask(void* pvParameters);

void CAN::setup()
{
    // twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17, GPIO_NUM_18, TWAI_MODE_NORMAL);
    twai_general_config_t g_config = {
        .mode = TWAI_MODE_NORMAL,          // Set the mode to normal operation
        .tx_io = GPIO_NUM_17,              // Transmit GPIO pin
        .rx_io = GPIO_NUM_18,              // Receive GPIO pin
        .clkout_io = GPIO_NUM_NC,          // No CLKOUT pin used
        .bus_off_io = GPIO_NUM_NC,         // No bus off indicator pin used
        .tx_queue_len = 128,               // Length of the TX queue
        .rx_queue_len = 128,               // Length of the RX queue
        .alerts_enabled = 0,               // No alerts enabled by default
        .clkout_divider = 0,               // No CLKOUT divider used
        .intr_flags = ESP_INTR_FLAG_LEVEL1 // Set interrupt flags for the driver
    };

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // Accept all messages by default
    // twai_filter_config_t f_config = {
    //     .acceptance_code = (PEAK_CAN_ID << 25), // Set the acceptance code to the desired CAN ID
    //     .acceptance_mask = ~(PEAK_CAN_MASK << 25),
    //     .single_filter = true};

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK)
    {
        screen::displayError("CAN Driver Install Failed", 5000);
        Serial.println("CAN Driver Install Error: " + String(esp_err_to_name(err)));
        return;
    }

    esp_err_t start_err = twai_start();
    if (start_err != ESP_OK)
    {
        Serial.println("CAN Start Error: " + String(esp_err_to_name(start_err)));
        screen::displayError("CAN Start Failed", 5000);
        return;
    }

    xTaskCreatePinnedToCore(
        CANTask,   // Task function
        "CANTask", // Name
        8192,      // Stack size
        NULL,      // Parameters
        1,         // Priority
        NULL,      // Task handle
        IO_CORE);  // Core ID
}

void CANTask(void* pvParameters)
{
    (void)pvParameters; // Unused parameter

    for (;;)
    {
        twai_message_t message;
        esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(1000)); // Wait for a message with a timeout of 100 ms
        if (err == ESP_OK)
        {
            bool processed = false; // Flag to check if the message was processed
            for (int i = 0; i < MAX_CAN_CALLBACKS; i++)
            {
                if (canCallbacks[i].id == (message.identifier & canCallbacks[i].mask)) // Check if the ID matches any registered callback
                {
                    if (canCallbacks[i].callback != nullptr) // Ensure the callback is not null
                    {
                        processed = true;                                                                     // Set processed to true if a callback is found
                        canCallbacks[i].callback(message.identifier, message.data, message.data_length_code); // Call the registered callback
                        break;                                                                                // Exit the loop after processing the message
                    }
                }
            }

            if (!processed) // If no callback was found for the message
            {
                Serial.printf("Received CAN message with ID: 0x%X, Data Length: %d, Data: ", message.identifier, message.data_length_code);
                for (int i = 0; i < message.data_length_code; i++)
                {
                    Serial.printf("0x%02X ", message.data[i]);
                }
                Serial.println("");
            }
        }

        vTaskDelay(1 / portTICK_PERIOD_MS); // Adjust delay as needed
    }

    vTaskDelete(NULL); // Delete the task when done
}

bool CAN::sendMessage(uint32_t id, const uint8_t* data, size_t length, bool extended)
{
    twai_message_t message;
    message.identifier = id;
    message.extd = extended ? 1 : 0; // Set the extended flag based on the parameter
    message.data_length_code = length;

    memcpy(message.data, data, length);
    message.flags = TWAI_MSG_FLAG_EXTD;

    esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(100)); // Attempt to send the message with a timeout of 100 ms
    if (err != ESP_OK)
    {
        Serial.printf("Failed to send CAN message: %s\n", esp_err_to_name(err));
        return false; // Return false if sending the message fails
    }

    return true;
}

void CAN::registerCallback(uint32_t id, uint32_t mask, void (*callback)(uint32_t id, const uint8_t* data, size_t length))
{
    for (int i = 0; i < MAX_CAN_CALLBACKS; i++)
    {
        if (canCallbacks[i].id == 0) // Find an empty slot
        {
            canCallbacks[i].id = id;
            canCallbacks[i].callback = callback;
            canCallbacks[i].mask = mask; // Set the mask to match the PEAK_CAN_ID
            return;                      // Callback registered successfully
        }
    }
    Serial.println("Error: Maximum number of CAN callbacks reached.");
}

void CAN::unregisterCallback(uint32_t id)
{
    for (int i = 0; i < MAX_CAN_CALLBACKS; i++)
    {
        if (canCallbacks[i].id == id) // Find the callback to remove
        {
            canCallbacks[i].id = 0;             // Clear the ID to mark it as unused
            canCallbacks[i].callback = nullptr; // Clear the callback function
            canCallbacks[i].mask = 0xFFFFFFFF;  // Reset the mask to default value
            return;                             // Callback unregistered successfully
        }
    }
    Serial.println("Error: Callback not found.");
}