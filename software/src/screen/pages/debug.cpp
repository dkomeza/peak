#include "debug.h"
#include "connection/CAN.h"

#include "screen/components/components.h"

Debug::Debug(TFT_eSPI *tft) : Page()
{
    // Initialize components for the home page
    addComponent(new TextBox(tft, 12, 12, tft->width() - 24, 16, CAN::canDebug)); // Example battery component
    // Add more components as needed
}

Debug::~Debug()
{
    clearComponents(); // Clear all components when the page is destroyed
}

void Debug::handleInput(Event *e)
{
}