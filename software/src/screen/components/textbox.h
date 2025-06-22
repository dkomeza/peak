#pragma once

#include "component.h"

class TextBox : public Component
{
public:
    TextBox(TFT_eSPI *tft, int x, int y, int width, int height, String &text);
    ~TextBox();

    void draw() override;

private:
    String &text; // Pointer to the text to be displayed
};