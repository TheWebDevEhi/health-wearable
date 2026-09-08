#include "power_mgr.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "../config.h"

void PowerMgr::begin() {
    // Wake on the LIS3DH double-tap interrupt (GPIO1) or Button 1 (GPIO21).
    // Both are within the ESP32-S3's RTC GPIO range (0-21), which EXT1
    // wakeup requires; Button 2 (GPIO47) is not RTC-capable and so is not a
    // wake source (readme.md #8.3). TODO: confirm esp_sleep_enable_ext1_wakeup
    // is still the current API on whatever arduino-esp32/esp-idf version this
    // builds against — newer esp-idf releases have been migrating to
    // esp_sleep_enable_ext1_wakeup_io().
    esp_sleep_enable_ext1_wakeup(
        (1ULL << PIN_LIS3DH_INT1) | (1ULL << PIN_BUTTON_1),
        ESP_EXT1_WAKEUP_ANY_HIGH);
    _lastActivityMs = millis();
}

void PowerMgr::update() {
    // TODO: replace with the real idle timeout once it's exposed on the
    // Settings screen; this is a structural stub only.
}

void PowerMgr::noteActivity() {
    _lastActivityMs = millis();
}

void PowerMgr::enterDeepSleep() {
    esp_deep_sleep_start();
}
