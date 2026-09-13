#pragma once

#include <XPT2046_Touchscreen.h>

#include "../config.h"

// XPT2046 resistive touch, sharing the SPI bus with the display via its own
// chip-select (readme.md #8.1). Drives the bottom nav bar and Detail-metric
// cycling (readme.md #5); the single physical button covers full-screen
// navigation independently, so touch miscalibration degrades rather than
// blocks the UI. See main.cpp's calibration flow and pollTouch().
class TouchXpt2046 {
public:
    // Must be called after SPI.begin(...) in main.cpp has already set the
    // shared bus's custom pins. begin() internally calls SPI.begin() too,
    // but arduino-esp32's SPIClass::begin() no-ops if the bus is already
    // initialized, so it won't reset the pins back to the peripheral's
    // hardware defaults — verified against arduino-esp32's SPI.cpp source.
    bool begin();
    bool pressed();

    // Raw ADC point (with TOUCH_SWAP_XY applied, since that's a wiring/
    // mounting fact about the panel, not part of calibration). No
    // calibration mapping — see readCalibrated() for that.
    bool readRaw(uint16_t &x, uint16_t &y);

    // Two-point linear calibration: (rawX0,rawY0) is the raw reading
    // measured when the panel was touched at screen position
    // (screenX0,screenY0), and likewise for point 1. The two screen points
    // must be well separated (diagonal corners) for the fit to mean
    // anything — see the calibration flow in main.cpp, which is the only
    // intended caller of this.
    void setCalibration(uint16_t rawX0, uint16_t rawY0, int16_t screenX0, int16_t screenY0,
                         uint16_t rawX1, uint16_t rawY1, int16_t screenX1, int16_t screenY1);

    // Applies the calibration from setCalibration() to a live reading.
    // Returns false (x/y untouched) if not currently touched, or if
    // setCalibration() was never called this boot.
    bool readCalibrated(int16_t &x, int16_t &y);

private:
    XPT2046_Touchscreen _touch{PIN_TOUCH_CS, PIN_TOUCH_IRQ};

    bool _calibrated = false;
    uint16_t _rawX0 = 0, _rawY0 = 0, _rawX1 = 0, _rawY1 = 0;
    int16_t _screenX0 = 0, _screenY0 = 0, _screenX1 = 0, _screenY1 = 0;
};
