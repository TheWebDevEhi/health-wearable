#pragma once

#include <Arduino.h>

// Runtime-editable settings written by the companion app over BLE
// (readme.md #7) and persisted in NVS. The device deep-sleeps regularly
// (see power/power_mgr.*), which wipes ordinary RAM, so anything that must
// survive that has to live somewhere else — NVS via the Preferences
// library, not a plain member variable. Falls back to config.h's
// DEFAULT_DEVICE_NAME and credentials.h's Wi-Fi placeholders until the app
// has actually written something.
class SettingsStore {
public:
    void begin();

    String deviceName() const { return _deviceName; }
    void setDeviceName(const String &name);

    String wifiSsid() const { return _wifiSsid; }
    String wifiPassword() const { return _wifiPassword; }
    void setWifiCredentials(const String &ssid, const String &password);

    // Two-point touch calibration (raw ADC readings only — the screen-space
    // points they correspond to are fixed by config.h's TOUCH_CAL_MARGIN,
    // so there's nothing per-unit to persist there). false/all-zero until
    // the one-time calibration flow in main.cpp actually runs.
    bool touchCalibrated() const { return _touchCalibrated; }
    void touchCalibrationRaw(uint16_t &rawX0, uint16_t &rawY0, uint16_t &rawX1, uint16_t &rawY1) const {
        rawX0 = _touchRawX0;
        rawY0 = _touchRawY0;
        rawX1 = _touchRawX1;
        rawY1 = _touchRawY1;
    }
    void setTouchCalibration(uint16_t rawX0, uint16_t rawY0, uint16_t rawX1, uint16_t rawY1);

    // Index into config.h's BRIGHTNESS_PERCENTS/BRIGHTNESS_LABELS, not a
    // raw percent — see the comment there for why.
    uint8_t brightnessLevel() const { return _brightnessLevel; }
    void setBrightnessLevel(uint8_t level);

private:
    String _deviceName;
    String _wifiSsid;
    String _wifiPassword;

    bool _touchCalibrated = false;
    uint16_t _touchRawX0 = 0;
    uint16_t _touchRawY0 = 0;
    uint16_t _touchRawX1 = 0;
    uint16_t _touchRawY1 = 0;

    uint8_t _brightnessLevel = 0;
};
