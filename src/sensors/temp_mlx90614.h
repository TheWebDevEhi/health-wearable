#pragma once

#include <Adafruit_MLX90614.h>
#include <Wire.h>

// MLX90614 non-contact IR temperature, treated as a trend rather than a
// clinical reading (readme.md #3).
class TempMlx90614 {
public:
    bool begin(TwoWire &bus);
    void update();

    float skinTempC() const { return _skinTempC; }

private:
    Adafruit_MLX90614 _mlx;
    float _skinTempC = 0.0f;

    // Moving average over the last few samples (readme.md #3).
    static constexpr int kSmoothingSamples = 4;
    float _smoothBuffer[kSmoothingSamples] = {};
    int _smoothIndex = 0;
    int _smoothCount = 0;
};
