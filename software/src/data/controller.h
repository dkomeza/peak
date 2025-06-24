#pragma once

#include <Arduino.h>

typedef enum
{
    CYCLEIQ_MODE_PAS = 0,
    CYCLEIQ_MODE_TORQUE,
    CYCLEIQ_MODE_HYBRID,
} cycleiq_support_mode_t;

typedef enum
{
    CYCLEIQ_RIDE_MODE_NORMAL = 0,
    CYCLEIQ_RIDE_MODE_MOUNTAIN,
} cycleiq_ride_mode_t;

typedef struct
{
    float max_speed;
    float battery_internal_resistance;
} cycleiq_config_t;

typedef struct
{
    uint8_t battery_level;
    float battery_voltage;
    float battery_current; // No regen so this is always positive
    float watt_hours;
    float amp_hours;

    int16_t motor_temperature;
    int16_t controller_temperature;
    float motor_current;
    uint16_t motor_power;

    uint8_t current_gear;
    cycleiq_support_mode_t support_mode;
    cycleiq_ride_mode_t ride_mode;

    float speed;
    float rpm;
} cycleiq_data_t;