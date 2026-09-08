#include "screen.h"

#include <Arduino.h>

#include "../config.h"

void Screen::begin() {
    _tft.init();
    _tft.setRotation(0);  // TODO confirm orientation at bring-up (readme.md #11.1)

    pinMode(PIN_TFT_BL, OUTPUT);
    setBacklight(100);
}

void Screen::setBacklight(uint8_t dutyPercent) {
    // TODO: switch to ledc PWM once the brightness curve is tuned; this
    // placeholder only supports fully on/off (readme.md #4 Settings screen).
    digitalWrite(PIN_TFT_BL, dutyPercent > 0 ? HIGH : LOW);
}
