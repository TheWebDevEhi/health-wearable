#pragma once

#include <XPT2046_Touchscreen.h>

#include "../config.h"

// XPT2046 resistive touch, sharing the SPI bus with the display via its own
// chip-select (readme.md #8.1). Secondary control input; the two side
// buttons are primary (readme.md #4).
class TouchXpt2046 {
public:
    // Must be called after SPI.begin(...) in main.cpp has already set the
    // shared bus's custom pins. begin() internally calls SPI.begin() too,
    // but arduino-esp32's SPIClass::begin() no-ops if the bus is already
    // initialized, so it won't reset the pins back to the peripheral's
    // hardware defaults — verified against arduino-esp32's SPI.cpp source.
    bool begin();
    bool pressed();

    // Raw ADC point; calibration to panel coordinates is TODO (readme.md #11).
    bool readRaw(uint16_t &x, uint16_t &y);

private:
    XPT2046_Touchscreen _touch{PIN_TOUCH_CS, PIN_TOUCH_IRQ};
};
