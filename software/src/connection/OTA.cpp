#include "OTA.h"
#include <WiFi.h>
#include "datatypes.h"
#include "VESC.h"
#include "BLE.h"

#define TCP_PORT 65102

long lastUpdate = 0;
int updateInterval = 200;

WiFiServer server(TCP_PORT);
WiFiClient client;

void TCPServerTask(void* pvParameters)
{
    (void)pvParameters; // Unused parameter

    VESC::TxPacket txPacket;

    while (true)
    {
        client = server.available(); // Check for new client connections
        if (client)
        {
            BLE::println("New client connected");
            while (client.connected())
            {
                bool hasData = false;
                if (client.available())
                {
                    hasData = true;
                }
                if (hasData) {
                    while (client.available()) {
                        uint8_t data = client.read();
                        // Serial.printf("%02X ", data); // Print each byte of the data
                        VESC::handleIncomingData(data); // Process incoming data
                    }
                }

                while (xQueueReceive(VESC::packetQueue, &txPacket, 0) == pdTRUE)
                {
                    if (txPacket.length > 0)
                    {
                        client.write(txPacket.data, txPacket.length); // Send the packet to the client
                    }
                    delay(1);
                }

                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            client.stop();
            BLE::println("Client disconnected");
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL); // Delete the task when done
}

namespace OTA
{
    IPAddress setup()
    {
        ArduinoOTA.setHostname("PEAK");

        WiFi.mode(WIFI_STA);
        WiFi.begin(credentials::ssid, credentials::password);

        long start = millis();

        while (millis() - start < 1000 && WiFi.waitForConnectResult() != WL_CONNECTED)
        {
            delay(100);
        }

        ArduinoOTA.begin();
        server.begin();
        server.setNoDelay(true);

        xTaskCreatePinnedToCore(
            updateTask,      // Task function
            "OTAUpdateTask", // Name
            8192,            // Stack size
            NULL,            // Parameters
            1,               // Priority
            NULL,            // Task handle
            IO_CORE);        // Core ID

        xTaskCreatePinnedToCore(
            TCPServerTask,   // Task function
            "TCPServerTask", // Name
            8192,            // Stack size
            NULL,            // Parameters
            2,               // Priority
            NULL,            // Task handle
            IO_CORE);        // Core ID

        return WiFi.localIP();
    }

    void updateTask(void* pvParameters)
    {
        (void)pvParameters; // Unused parameter

        while (true)
        {
            ArduinoOTA.handle();
            vTaskDelay(updateInterval / portTICK_PERIOD_MS);
        }

        vTaskDelete(NULL); // Delete the task when done
    }
}