#pragma once

#include <Arduino.h>
#include "pages/page.h"

namespace screen
{
    void setup(bool crashRecovery = false);

    void setBrightness(int brightness);
    void setBrightness(int brightness, int duration);
    void setPage(Page *page);

    void displayError(const char *message, int duration = 5000);

    extern TFT_eSPI tft; // Global TFT instance
}