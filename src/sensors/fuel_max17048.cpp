#include "fuel_max17048.h"

bool FuelMax17048::begin(TwoWire &bus) {
    // Before joining the shared bus, confirm whether this module's I2C
    // pull-ups reference VBAT or a regulated 3.3 V (readme.md #11.3) — add a
    // level shifter if needed.
    return _fuel.begin(&bus);
}

void FuelMax17048::update() {
    _batteryPercent = _fuel.cellPercent();
    _batteryLow = _batteryPercent <= kLowBatteryPercent;
}
