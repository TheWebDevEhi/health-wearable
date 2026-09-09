#pragma once

#include <MAX30105.h>
#include <Wire.h>

// MAX30102 PPG: heart rate + SpO2, with accelerometer-gated motion rejection
// (readme.md #3). Uses the ratio-of-ratios algorithm bundled with the
// SparkFun MAX3010x library (spo2_algorithm.h). The library's own class is
// named MAX30105 (it covers the whole MAX3010x family, MAX30102 included).
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
    // 100 samples at ~100 Hz is the window size the reference SpO2 example
    // uses; keep it unless bring-up data says otherwise (readme.md #3).
    static constexpr int kBufferLength = 100;

    MAX30105 _sensor;
    uint32_t _irBuffer[kBufferLength] = {};
    uint32_t _redBuffer[kBufferLength] = {};
    int _sampleIndex = 0;

    float _heartRateBpm = 0.0f;
    float _spo2Percent = 0.0f;
    bool _signalValid = false;

    void runAlgorithm();
};
