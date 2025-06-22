#include "crash.h"
#include <Arduino.h>

bool bootCrashGuard()
{
    esp_reset_reason_t resetReason = esp_reset_reason();

    if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT)
        return true; // Indicate that we are in crash recovery mode
    else
        return false; // Normal boot
}