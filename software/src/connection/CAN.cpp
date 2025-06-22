#include "CAN.h"
#include "defines.h"
#include "screen/screen.h"

#include <driver/twai.h>

#define PEAK_CAN_ID 0x6Au
#define PEAK_CAN_MASK 0x7Fu

#define CYCLEIQ_CAN_ID 0x6Bu

using namespace CAN;

String CAN::canDebug = ""; // Initialize the debug string for CAN messages

void CANTask(void *pvParameters);

void CAN::setup()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17, GPIO_NUM_18, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    twai_filter_config_t f_config = {
        .acceptance_code = (PEAK_CAN_ID << 25), // Set the acceptance code to the desired CAN ID
        .acceptance_mask = ~(PEAK_CAN_MASK << 25),
        .single_filter = true};

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

void CANTask(void *pvParameters)
{
    (void)pvParameters; // Unused parameter

    for (;;)
    {
        twai_message_t message;
        esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(1000)); // Wait for a message with a timeout of 100 ms
        if (err == ESP_OK)
        {
            // String debugMessage = "ID: " + String(message.identifier, HEX) + ", Data: ";

            uint16_t battery_voltage_int = 0;
            battery_voltage_int = (message.data[0] << 8) | message.data[1]; // Combine the first two bytes for battery voltage
            float battery_voltage = battery_voltage_int / 100.0;            // Convert to volts
            // debugMessage += "Battery Voltage: " + String(battery_voltage, 2) + "V, ";

            uint8_t pas_state = message.data[2];                            // Get the pedal assist state from the third byte
            uint16_t int_torque = (message.data[3] << 8) | message.data[4]; // Combine the fourth and fifth bytes for torque
            float torque = int_torque / 1000.0;                             // Convert to Volts

            bool is_pedaling = message.data[5]; // Check if the first bit of the sixth byte is set (indicating pedaling)

            Serial.printf(">pas:%d\n", pas_state);
        }
        else // If the error is not a timeout, print the error
        {
            Serial.println("CAN receive error: " + String(esp_err_to_name(err)));
        }

        // Process the received message
        twai_message_t new_message;
        new_message.identifier = CYCLEIQ_CAN_ID << 4 | (0x1 & 0xF); // Set the identifier for CycleIQ messages
        new_message.extd = false;                                   // Use standard frame format (11-bit ID)
        new_message.data_length_code = 8;
        new_message.data[0] = 0xDE;
        new_message.data[1] = 0xAD;
        new_message.data[2] = 0xBE;
        new_message.data[3] = 0xEF;
        new_message.data[4] = 0x00;
        new_message.data[5] = 0x01;
        new_message.data[6] = 0x02;
        new_message.data[7] = 0x03;
        new_message.flags = TWAI_MSG_FLAG_NONE;

        esp_err_t send_err = twai_transmit(&new_message, pdMS_TO_TICKS(100)); // Attempt to send a message with a timeout of 100 ms
        // if (send_err == ESP_OK)
        // {
        //     Serial.println("Message sent successfully");
        // }
        // else
        // {
        //     Serial.printf("Failed to send message: %s\n", esp_err_to_name(send_err));
        // }

        vTaskDelay(10 / portTICK_PERIOD_MS); // Adjust delay as needed
    }

    vTaskDelete(NULL); // Delete the task when done
}
