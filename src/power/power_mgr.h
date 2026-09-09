#pragma once

#include <cstdint>

class Screen;  // see attachScreen()

// Deep sleep, double-tap/button wake, and backlight timeout
// (readme.md #5, #6).
class PowerMgr {
public:
    void begin();

    // Screen is wired in separately from begin() since it isn't constructed
    // yet at PowerMgr::begin() time in main.cpp's setup() ordering. Turned
    // off just before deep sleep.
    void attachScreen(Screen &screen);

    // Called periodically by the power task; enters deep sleep once the
    // idle timeout elapses.
    void update();

    // Call whenever a button, touch, or BLE event should reset the idle
    // timer.
    void noteActivity();

private:
    Screen *_screen = nullptr;
    uint32_t _lastActivityMs = 0;

    void enterDeepSleep();
};
