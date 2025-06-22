#pragma once

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "credentials.h"

namespace OTA
{
    IPAddress setup();
    void updateTask(void *pvParameters);
}