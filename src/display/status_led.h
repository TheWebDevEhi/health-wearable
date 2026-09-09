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

    // Blinking red; call tick() regularly to advance it. Idempotent while
    // already in Alert mode — callers are expected to call this on every
    // tick they want the alert state active (main.cpp does), and it must
    // not reset the blink phase back to "on" each time or it fights
    // tick()'s own toggle and the LED never visibly blinks.
    void setAlert();

    // Advances the blink phase for setAlert(); a no-op in other states.
    // Call from the display task's own tick.
    void tick();

private:
    Adafruit_NeoPixel _pixel{1, PIN_STATUS_LED, NEO_GRB + NEO_KHZ800};
    enum class Mode { Off, Ok, Alert } _mode = Mode::Off;
    bool _blinkOn = false;
};
