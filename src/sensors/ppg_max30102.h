#pragma once

#include <Wire.h>

// MAX30102 PPG: heart rate + SpO2, with accelerometer-gated motion rejection
// (readme.md #3). Sampling and the peak/ratio calculation are TODO — this is
// currently a structural stub.
class PpgMax30102 {
public:
    bool begin(TwoWire &bus);

    // Call from the sensor task on its sample timer. `armMoving` should come
    // from MotionLis3dh so noisy windows get rejected and the last good
    // value held instead of shown.
    void update(bool armMoving);

    float heartRateBpm() const { return _heartRateBpm; }
    float spo2Percent() const { return _spo2Percent; }
    bool signalValid() const { return _signalValid; }

private:
    float _heartRateBpm = 0.0f;
    float _spo2Percent = 0.0f;
    bool _signalValid = false;
};
