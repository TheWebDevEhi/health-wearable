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
