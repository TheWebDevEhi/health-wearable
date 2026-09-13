#include "screen.h"

#include <Arduino.h>

#include "../config.h"

namespace {
constexpr uint32_t kBacklightFreqHz = 5000;
constexpr uint8_t kBacklightResolutionBits = 8;
}  // namespace

void Screen::begin() {
    _tft.init();
    _tft.setRotation(0);  // TODO confirm orientation at bring-up (readme.md #11.1)

    ledcSetup(kBacklightChannel, kBacklightFreqHz, kBacklightResolutionBits);
    ledcAttachPin(PIN_TFT_BL, kBacklightChannel);
    setBacklight(100);
}

void Screen::setBacklight(uint8_t dutyPercent) {
    uint32_t duty = (static_cast<uint32_t>(dutyPercent) * ((1 << kBacklightResolutionBits) - 1)) / 100;
    ledcWrite(kBacklightChannel, duty);
}
