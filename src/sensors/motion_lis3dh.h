#pragma once

#include <Adafruit_LIS3DH.h>
#include <Wire.h>

// LIS3DH: steps, activity state, double-tap wake, fall flag (readme.md #3,
// #5). Also the sensor the firmware leans on to reject motion-corrupted PPG
// windows.
class MotionLis3dh {
public:
    bool begin(TwoWire &bus);

    // Configures the hardware double-tap engine so the sensor can wake the
    // chip on GPIO1 while the MCU is in deep sleep.
    bool configureDoubleTapWake();

    void update();

    uint32_t stepCount() const { return _stepCount; }
    bool isMoving() const { return _isMoving; }
    bool fallDetected() const { return _fallDetected; }

private:
    // Adafruit_LIS3DH binds its I2C bus at construction, not at begin(), so
    // this only supports the global Wire instance — the only bus this
    // hardware has (readme.md #8).
    Adafruit_LIS3DH _lis{&Wire};

    uint32_t _stepCount = 0;
    bool _isMoving = false;
    bool _fallDetected = false;

    // Movement threshold on |acceleration - 1g|; not a step-counting
    // algorithm yet — see update().
    static constexpr float kMovementThresholdG = 0.15f;
};
