#pragma once

#include <Arduino.h>

#include "page.h"

class Debug : public Page
{
public:
    Debug(TFT_eSPI *tft);
    ~Debug();

    void handleInput(Event *e) override;
};