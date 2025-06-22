#include "OTA.h"
#include "defines.h"

long lastUpdate = 0;
int updateInterval = 200;

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

        xTaskCreatePinnedToCore(
            updateTask,      // Task function
            "OTAUpdateTask", // Name
            8192,            // Stack size
            NULL,            // Parameters
            1,               // Priority
            NULL,            // Task handle
            IO_CORE);        // Core ID

        return WiFi.localIP();
    }

    void updateTask(void *pvParameters)
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