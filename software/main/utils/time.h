#ifndef TIME_H
#define TIME_H

#include <esp_timer.h>

#define millis() (esp_timer_get_time() / 1000ULL)

#endif
