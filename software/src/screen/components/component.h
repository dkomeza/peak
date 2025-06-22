#pragma once

#include <TFT_eSPI.h>

#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))

class Component
{
public:
    Component(TFT_eSPI *tft, int x, int y, int width, int height);
    virtual ~Component();

    virtual void draw();

protected:
    TFT_eSPI *tft;
    TFT_eSprite *sprite;
    int x;
    int y;
    int width;
    int height;

    inline bool canDraw() const
    {
        return sprite != nullptr && sprite->created();
    }
};