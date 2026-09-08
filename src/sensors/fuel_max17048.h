#pragma once

#include <Wire.h>

// MAX17048 fuel gauge: battery percentage and a low-battery flag
// (readme.md #3). Reads VBAT directly, upstream of the buck-boost.
class FuelMax17048 {
public:
    bool begin(TwoWire &bus);
    void update();

    float batteryPercent() const { return _batteryPercent; }
    bool batteryLow() const { return _batteryLow; }

private:
    float _batteryPercent = 0.0f;
    bool _batteryLow = false;
};
