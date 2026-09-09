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

private:
    String _deviceName;
    String _wifiSsid;
    String _wifiPassword;
};
