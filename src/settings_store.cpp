#include "settings_store.h"

#include <Preferences.h>

#include "config.h"
#include "credentials.h"

namespace {
// NVS namespace — max 15 chars, ESP-IDF limit.
constexpr const char *kNamespace = "keis";
constexpr const char *kKeyDeviceName = "name";
constexpr const char *kKeyWifiSsid = "ssid";
constexpr const char *kKeyWifiPass = "pass";
constexpr const char *kKeyTouchCalOk = "tcalok";
constexpr const char *kKeyTouchCalX0 = "tcalx0";
constexpr const char *kKeyTouchCalY0 = "tcaly0";
constexpr const char *kKeyTouchCalX1 = "tcalx1";
constexpr const char *kKeyTouchCalY1 = "tcaly1";
constexpr const char *kKeyBrightness = "bright";
}  // namespace

void SettingsStore::begin() {
    // On the very first boot ever, the "keis" namespace doesn't exist yet,
    // and opening it readOnly=true in that state fails (nvs_open returns an
    // error rather than creating it — confirmed against arduino-esp32's own
    // Preferences.cpp, not assumed). That's fine here specifically: the
    // return value is intentionally unchecked because every getString()
    // below independently guards on Preferences' own `_started` flag and
    // falls back to the provided default when begin() didn't succeed — so
    // first boot correctly yields the config.h/credentials.h defaults
    // without needing an explicit check here. Don't copy this
    // ignore-the-return-value pattern into setDeviceName()/
    // setWifiCredentials() below, which use readOnly=false and must
    // actually succeed to persist anything.
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/true);
    _deviceName = prefs.getString(kKeyDeviceName, DEFAULT_DEVICE_NAME);
    _wifiSsid = prefs.getString(kKeyWifiSsid, kWifiSsid);
    _wifiPassword = prefs.getString(kKeyWifiPass, kWifiPassword);
    _touchCalibrated = prefs.getBool(kKeyTouchCalOk, false);
    _touchRawX0 = prefs.getUShort(kKeyTouchCalX0, 0);
    _touchRawY0 = prefs.getUShort(kKeyTouchCalY0, 0);
    _touchRawX1 = prefs.getUShort(kKeyTouchCalX1, 0);
    _touchRawY1 = prefs.getUShort(kKeyTouchCalY1, 0);
    // Default to the highest level (index count-1), matching the fully-on
    // backlight this screen always started at before brightness control
    // existed.
    _brightnessLevel = prefs.getUChar(kKeyBrightness, BRIGHTNESS_LEVEL_COUNT - 1);
    if (_brightnessLevel >= BRIGHTNESS_LEVEL_COUNT) {
        // Defensive: a stale NVS value from a firmware version with more
        // levels than this one shouldn't index out of bounds.
        _brightnessLevel = BRIGHTNESS_LEVEL_COUNT - 1;
    }
    prefs.end();
}

void SettingsStore::setDeviceName(const String &name) {
    _deviceName = name;
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putString(kKeyDeviceName, name);
    prefs.end();
}

void SettingsStore::setWifiCredentials(const String &ssid, const String &password) {
    _wifiSsid = ssid;
    _wifiPassword = password;
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putString(kKeyWifiSsid, ssid);
    prefs.putString(kKeyWifiPass, password);
    prefs.end();
}

void SettingsStore::setTouchCalibration(uint16_t rawX0, uint16_t rawY0, uint16_t rawX1, uint16_t rawY1) {
    _touchRawX0 = rawX0;
    _touchRawY0 = rawY0;
    _touchRawX1 = rawX1;
    _touchRawY1 = rawY1;
    _touchCalibrated = true;
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putUShort(kKeyTouchCalX0, rawX0);
    prefs.putUShort(kKeyTouchCalY0, rawY0);
    prefs.putUShort(kKeyTouchCalX1, rawX1);
    prefs.putUShort(kKeyTouchCalY1, rawY1);
    prefs.putBool(kKeyTouchCalOk, true);
    prefs.end();
}

void SettingsStore::setBrightnessLevel(uint8_t level) {
    if (level >= BRIGHTNESS_LEVEL_COUNT) {
        level = BRIGHTNESS_LEVEL_COUNT - 1;
    }
    _brightnessLevel = level;
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putUChar(kKeyBrightness, level);
    prefs.end();
}
