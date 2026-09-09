#pragma once

#include <cstdint>

class Screen;   // see attachScreen()
class BleGatt;  // see attachBle()

// Deep sleep, double-tap/button wake, and backlight timeout
// (readme.md #5, #6).
class PowerMgr {
public:
    void begin();

    // Screen is wired in separately from begin() since it isn't constructed
    // yet at PowerMgr::begin() time in main.cpp's setup() ordering. Turned
    // off just before deep sleep.
    void attachScreen(Screen &screen);

    // Lets update() use CONNECTED_IDLE_TIMEOUT_MS instead of
    // IDLE_TIMEOUT_MS while a client is connected — without this, the band
    // deep-sleeps and drops an active BLE connection ~15s after the last
    // button press, which defeats the point of being connected at all.
    void attachBle(BleGatt &ble);

    // Called periodically by the power task; enters deep sleep once the
    // idle timeout elapses.
    void update();

    // Call whenever a button, touch, or BLE event should reset the idle
    // timer.
    void noteActivity();

private:
    Screen *_screen = nullptr;
    BleGatt *_ble = nullptr;
    uint32_t _lastActivityMs = 0;

    void enterDeepSleep();
};
