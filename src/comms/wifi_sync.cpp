#include "wifi_sync.h"

#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

#include "../credentials.h"
#include "../settings_store.h"

namespace {
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
}  // namespace

void WifiSync::attachSettingsStore(SettingsStore &settings) {
    _settings = &settings;
}

bool WifiSync::connectWifi() {
    String ssid = (_settings != nullptr) ? _settings->wifiSsid() : String(kWifiSsid);
    String password = (_settings != nullptr) ? _settings->wifiPassword() : String(kWifiPassword);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > kWifiConnectTimeoutMs) {
            return false;
        }
        delay(200);
    }
    return true;
}

void WifiSync::disconnectWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool WifiSync::startOtaUpdate() {
    if (!connectWifi()) {
        disconnectWifi();
        return false;
    }

    WiFiClient client;
    t_httpUpdate_return result = httpUpdate.update(client, kOtaUpdateUrl);

    disconnectWifi();

    if (result == HTTP_UPDATE_OK) {
        return true;
    }
    Serial.printf("[OTA] update failed: %s\n", httpUpdate.getLastErrorString().c_str());
    return false;
}
