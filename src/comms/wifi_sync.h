#pragma once

// Wi-Fi is switched on only for OTA firmware updates (readme.md #7) — never
// called from the hot loop.
class WifiSync {
public:
    bool startOtaUpdate();
    bool inProgress() const { return _inProgress; }

private:
    bool _inProgress = false;
};
