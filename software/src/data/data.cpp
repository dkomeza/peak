#include "data.h"
#include <EEPROM.h>

#define EEPROM_SIZE 15

bool validate_data()
{
    int crc = 0;
    for (int i = 0; i < EEPROM_SIZE; i++)
    {
        crc ^= EEPROM.read(i);
    }

    int stored_crc = EEPROM.read(EEPROM_SIZE);
    return crc == stored_crc;
};

void setup_default_data()
{
    EEPROM.write(0, 69); // Battery level
    EEPROM.write(1, 54); // Battery voltage
    EEPROM.write(2, 0);  // Motor temperature
    EEPROM.write(3, 0);  // Speed
    EEPROM.write(4, 0);  // Power
    EEPROM.write(5, 0);  // Gear
    EEPROM.write(6, CYCLEIQ_MODE_PAS); // Support mode
    EEPROM.write(7, CYCLEIQ_RIDE_MODE_NORMAL); // Ride mode
    EEPROM.write(8, 0);  // Trip distance
    EEPROM.write(9, 0);  // Range

    int crc = 0;
    for (int i = 0; i < EEPROM_SIZE; i++)
    {
        crc ^= EEPROM.read(i);
    }
    EEPROM.write(EEPROM_SIZE, crc);

    EEPROM.commit();
};

void data::init()
{
    EEPROM.begin(EEPROM_SIZE);

    if (!validate_data())
        setup_default_data();
};