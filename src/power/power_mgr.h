#pragma once

// Deep sleep, double-tap/button wake, and backlight timeout
// (readme.md #5, #6).
class PowerMgr {
public:
    void begin();

    // Called periodically by the power task; enters deep sleep once the
    // idle timeout elapses.
    void update();

    // Call whenever a button, touch, or BLE event should reset the idle
    // timer.
    void noteActivity();

private:
    uint32_t _lastActivityMs = 0;

    void enterDeepSleep();
};
