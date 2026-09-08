#pragma once

#include <Wire.h>

// LIS3DH: steps, activity state, double-tap wake, fall flag (readme.md #3,
// #5). Also the sensor the firmware leans on to reject motion-corrupted PPG
// windows.
class MotionLis3dh {
public:
    bool begin(TwoWire &bus);

    // Configures the hardware double-tap engine (Adafruit setClick) so the
    // sensor can wake the chip on GPIO1 while the MCU is in deep sleep.
    bool configureDoubleTapWake();

    void update();

    uint32_t stepCount() const { return _stepCount; }
    bool isMoving() const { return _isMoving; }
    bool fallDetected() const { return _fallDetected; }

private:
    uint32_t _stepCount = 0;
    bool _isMoving = false;
    bool _fallDetected = false;
};
