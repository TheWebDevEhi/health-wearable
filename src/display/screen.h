#pragma once

#include <TFT_eSPI.h>

// Thin wrapper around TFT_eSPI for the shared ST7735 panel. Pin and tab
// configuration live in platformio.ini build_flags, not here.
class Screen {
public:
    void begin();

    // dutyPercent: 0-100. Real PWM via ledc (arduino-esp32 3.x's
    // channel-based API — ledcAttach(pin, freq, res) isn't in this
    // installed core version, confirmed against esp32-hal-ledc.h directly
    // rather than assumed), not the on/off placeholder this used to be.
    void setBacklight(uint8_t dutyPercent);

    TFT_eSPI &raw() { return _tft; }

private:
    static constexpr int kBacklightChannel = 0;

    TFT_eSPI _tft;
};
