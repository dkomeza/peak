#include <Arduino.h>
#include <TFT_eSPI.h>

#include "connection/OTA.h"
#include "screen/screen.h"
#include "connection/CAN.h"
#include "connection/BLE.h"
#include "connection/VESC.h"
#include "data/data.h"

#include "screen/pages/debug.h"

#include "utils/crash.h"

void setup()
{
  Serial.begin(921600);

  bool crashRecovery = bootCrashGuard();

  OTA::setup();
  screen::setup(crashRecovery);
  if (crashRecovery)
    return;

  data::init();
  VESC::setup();
  CAN::setup();
  BLE::setup();
}

void loop()
{
  vTaskDelay(500 / portTICK_PERIOD_MS); // Delay to prevent watchdog timeout
}