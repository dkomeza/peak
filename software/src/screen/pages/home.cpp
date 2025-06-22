#include "home.h"

#include "data/data.h"

#include "screen/components/battery.h"

Home::Home(TFT_eSPI *tft) : Page()
{
    // Initialize components for the home page
    addComponent(new Battery(tft, 10, 10, 80, 30, data::batteryLevel)); // Example battery component
    // Add more components as needed
}

Home::~Home()
{
    clearComponents(); // Clear all components when the page is destroyed
}

void Home::handleInput(Event *e)
{
}