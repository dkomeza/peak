#include <Arduino.h>
#include <TFT_eSPI.h>

#include "connection/OTA.h"
#include "screen/screen.h"
#include "connection/CAN.h"

#include "screen/pages/debug.h"

#include "utils/crash.h"

void setup()
{
  Serial.begin(115200);

  bool crashRecovery = bootCrashGuard();

  OTA::setup();
  screen::setup(crashRecovery);
  if (crashRecovery)
    return;

  CAN::setup();
}

void loop()
{
  vTaskDelay(100 / portTICK_PERIOD_MS); // Delay to prevent watchdog timeout
}