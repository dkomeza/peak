#pragma once

#include "component.h"
#include "data/data.h"

class Battery : public Component
{
public:
    Battery(TFT_eSPI *tft, int x, int y, int width, int height, data::AtomicByte &batteryLevel);
    ~Battery();

    void draw() override;

private:
    data::AtomicByte &batteryLevel;
};