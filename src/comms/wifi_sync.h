#pragma once

class SettingsStore;  // see attachSettingsStore()

// Wi-Fi is switched on only for OTA firmware updates (readme.md #7) — never
// called from the hot loop.
class WifiSync {
public:
    // Wi-Fi credentials come from here if attached (readme.md #7 — the
    // companion app can change them at runtime), falling back to
    // credentials.h otherwise.
    void attachSettingsStore(SettingsStore &settings);

    // Connects Wi-Fi, pulls a firmware image over HTTP via HTTPUpdate, then
    // disconnects Wi-Fi either way. Blocks until the attempt finishes —
    // call from a task that can afford to block (not sensor/display/BLE),
    // e.g. in response to the Settings screen's "sync now" action.
    bool startOtaUpdate();

private:
    SettingsStore *_settings = nullptr;

    bool connectWifi();
    void disconnectWifi();
};
