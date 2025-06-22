#pragma once

#include <Arduino.h>

enum class EventButton
{
    UP,
    DOWN,
    POWER,
};

enum class EventType
{
    CLICK,
    LONG_PRESS,
    PRESS_START,
    PRESS_END,
};

struct Event
{
    EventButton button;      // The button associated with the event
    EventType type;          // The type of the event (click, long press, etc.)
    unsigned long timestamp; // Timestamp of the event in milliseconds

    Event(EventButton btn, EventType eventType, unsigned long time)
        : button(btn), type(eventType), timestamp(millis()) {}
};
