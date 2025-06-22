#pragma once

#include <Arduino.h>

#include "page.h"

class Home : public Page
{
public:
    Home(TFT_eSPI *tft);
    ~Home();

    void handleInput(Event *e) override;
};