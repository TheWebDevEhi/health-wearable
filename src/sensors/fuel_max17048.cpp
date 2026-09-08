#include "fuel_max17048.h"

#include "../config.h"

bool FuelMax17048::begin(TwoWire &bus) {
    // TODO: initialize the Adafruit MAX1704X driver on I2C_ADDR_MAX17048.
    // Before joining the shared bus, confirm whether this module's I2C
    // pull-ups reference VBAT or a regulated 3.3 V (readme.md #11.3) — add a
    // level shifter if needed.
    (void)bus;
    return false;
}

void FuelMax17048::update() {
    // TODO: read state-of-charge and set _batteryLow against a threshold.
}
