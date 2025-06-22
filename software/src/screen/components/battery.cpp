#include "battery.h"

Battery::Battery(TFT_eSPI *tft, int x, int y, int width, int height, data::AtomicByte &batteryLevel)
    : Component(tft, x, y, width, height), batteryLevel(batteryLevel)
{
    sprite->loadFont("inter_14");
}

Battery::~Battery()
{
}

void Battery::draw()
{
    if (!canDraw())
        return;

    uint8_t batteryLevelValue = batteryLevel.load();

    uint16_t bgColor = RGB565(8, 8, 8);
    uint16_t iconColor = RGB565(48, 48, 48);
    uint16_t iconFillColor = RGB565(192, 192, 192);

    sprite->fillSprite(TFT_BLACK); // Clear the sprite with black background
    sprite->fillSmoothRoundRect(0, 0, width, height, 5, bgColor, TFT_BLACK);

    uint8_t icon_width = 24;
    uint8_t icon_height = 12;
    uint8_t icon_x = 6;
    uint8_t icon_y = (height - icon_height) / 2;
    uint8_t icon_fill_width = icon_width * batteryLevelValue / 100;

    sprite->fillSmoothRoundRect(icon_x, icon_y, icon_width, icon_height, 2, iconColor, bgColor);
    sprite->fillSmoothRoundRect(icon_x + icon_width, icon_y + (icon_height - 4) / 2, 3, 4, 1, iconColor, bgColor); // Draw the battery terminal
    sprite->fillSmoothRoundRect(icon_x, icon_y, icon_fill_width, icon_height, 2, iconFillColor, bgColor);          // Fill the battery level

    if (icon_fill_width <= icon_width - 2)                                                     // Fill the entire battery icon if the level is high enough
        sprite->drawRect(icon_x + icon_fill_width - 2, icon_y, 2, icon_height, iconFillColor); // Draw the right edge of the battery icon
    else if (icon_fill_width == icon_width)
        sprite->fillSmoothRoundRect(icon_x + icon_width, icon_y + (icon_height - 4) / 2, 3, 4, 1, iconFillColor, bgColor); // Draw the battery terminal full

    sprite->setTextColor(TFT_WHITE, bgColor);
    sprite->setTextSize(1);
    sprite->setTextDatum(MR_DATUM);
    sprite->setTextWrap(false);

    if (batteryLevelValue == 100)
        sprite->drawString(String(batteryLevelValue) + "%", this->width - 4, this->height / 2 - 3);
    else
        sprite->drawString(String(batteryLevelValue) + "%", this->width - 8, this->height / 2 - 3);

    sprite->pushSprite(x, y);
}