#pragma once

#include <Adafruit_MAX1704X.h>
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
    Adafruit_MAX17048 _fuel;
    float _batteryPercent = 0.0f;
    bool _batteryLow = false;

    // TODO: tune against the actual 523450 LiPo discharge curve (readme.md #5).
    static constexpr float kLowBatteryPercent = 15.0f;
};
