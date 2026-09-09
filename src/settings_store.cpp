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
