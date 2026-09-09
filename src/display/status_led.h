#pragma once

#include <Adafruit_NeoPixel.h>

#include "../config.h"

// On-board WS2812 status LED (readme.md #8.3, GPIO48). Not in the original
// §9 tree — added alongside UiScreens since "full-screen alert plus a
// blinking status LED" (readme.md #4) is one behaviour, split across two
// pieces of hardware.
class StatusLed {
public:
    void begin();

    void setOff();
    void setOk();     // steady, low-brightness — normal operation
    void setAlert();  // blinking red; call tick() regularly to advance it

    // Advances the blink phase for setAlert(); a no-op in other states.
    // Call from the display task's own tick.
    void tick();

private:
    Adafruit_NeoPixel _pixel{1, PIN_STATUS_LED, NEO_GRB + NEO_KHZ800};
    enum class Mode { Off, Ok, Alert } _mode = Mode::Off;
    bool _blinkOn = false;
};
