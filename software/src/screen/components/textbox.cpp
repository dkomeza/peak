#include "textbox.h"

TextBox::TextBox(TFT_eSPI *tft, int x, int y, int width, int height, String &text)
    : Component(tft, x, y, width, height), text(text)
{
    sprite->loadFont("inter_14");
}

TextBox::~TextBox()
{
}

void TextBox::draw()
{
    if (!canDraw())
        return;

    sprite->fillSprite(TFT_BLACK); // Clear the sprite with black background
    sprite->setTextColor(TFT_WHITE, TFT_BLACK);
    sprite->setTextSize(1);
    sprite->setTextDatum(TL_DATUM);
    sprite->setTextWrap(true);

    // Draw the text inside the textbox
    sprite->drawString(text, 0, 0);

    // Push the sprite to the display
    sprite->pushSprite(x, y);
}