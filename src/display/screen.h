#pragma once

#include <TFT_eSPI.h>

// Thin wrapper around TFT_eSPI for the shared ST7735 panel. Pin and tab
// configuration live in platformio.ini build_flags, not here.
class Screen {
public:
    void begin();
    void setBacklight(uint8_t dutyPercent);

    TFT_eSPI &raw() { return _tft; }

private:
    TFT_eSPI _tft;
};
