#pragma once

#include <Wire.h>

// MLX90614 non-contact IR temperature, treated as a trend rather than a
// clinical reading (readme.md #3).
class TempMlx90614 {
public:
    bool begin(TwoWire &bus);
    void update();

    float skinTempC() const { return _skinTempC; }

private:
    float _skinTempC = 0.0f;
};
