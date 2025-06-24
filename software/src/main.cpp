#include <Arduino.h>
#include <TFT_eSPI.h>

#include "connection/OTA.h"
#include "screen/screen.h"
#include "connection/CAN.h"
#include "connection/BLE.h"
#include "data/data.h"

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

  data::init();
  CAN::setup();
  BLE::setup();
}

void loop()
{
  vTaskDelay(500 / portTICK_PERIOD_MS); // Delay to prevent watchdog timeout
}