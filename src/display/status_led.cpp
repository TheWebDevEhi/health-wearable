#include "status_led.h"

void StatusLed::begin() {
    _pixel.begin();
    setOff();
}

void StatusLed::setOff() {
    _mode = Mode::Off;
    _pixel.setPixelColor(0, 0);
    _pixel.show();
}

void StatusLed::setOk() {
    _mode = Mode::Ok;
    _pixel.setPixelColor(0, _pixel.Color(0, 12, 0));  // dim green
    _pixel.show();
}

void StatusLed::setAlert() {
    if (_mode == Mode::Alert) {
        // Already alerting — do not reset the blink phase. main.cpp calls
        // this on every display-task tick while an alert is active; if this
        // forced blinkOn=true every time, tick()'s toggle right after would
        // immediately flip it back off, and the LED would never visibly
        // blink (this was bug #1 from the end-to-end analysis).
        return;
    }
    _mode = Mode::Alert;
    _blinkOn = true;
    _pixel.setPixelColor(0, _pixel.Color(40, 0, 0));  // red
    _pixel.show();
}

void StatusLed::tick() {
    if (_mode != Mode::Alert) {
        return;
    }
    _blinkOn = !_blinkOn;
    _pixel.setPixelColor(0, _blinkOn ? _pixel.Color(40, 0, 0) : 0);
    _pixel.show();
}
