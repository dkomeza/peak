#include "component.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

Component::Component(TFT_eSPI *tft, int x, int y, int width, int height)
{
    this->tft = tft;
    this->x = x;
    this->y = y;
    this->width = width;
    this->height = height;

    sprite = new TFT_eSprite(tft);
    sprite->setColorDepth(16);
    sprite->createSprite(width, height); // Create an oversized sprite for antialiasing
    // sprite->setOrigin(2, 2);                     // Set origin to (2, 2) for better positioning
}

Component::~Component()
{
    delete sprite;
}

void Component::draw()
{
    if (!canDraw())
    {
        Serial.println("Sprite not created or invalid.");
        return;
    }

    sprite->fillSprite(TFT_BLACK);
    sprite->setTextColor(TFT_WHITE);
    sprite->setTextSize(1);
    sprite->setTextFont(1); // Use a larger font for better visibility
    sprite->drawString("Hello, Component!", 0, 0);
    sprite->pushSprite(x, y);
}