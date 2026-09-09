#include "wifi_sync.h"

#include <Arduino.h>
#include <HTTPUpdate.h>
#include <WiFi.h>

namespace {

// TODO: no Wi-Fi network or OTA server exists for this project yet — these
// are placeholders so the build compiles. Before this ever runs for real,
// move actual credentials out of source control (e.g. a gitignored
// credentials.h) rather than editing them in here (readme.md #7).
constexpr const char *kWifiSsid = "TODO_SSID";
constexpr const char *kWifiPassword = "TODO_PASSWORD";
constexpr const char *kOtaUpdateUrl = "http://TODO_HOST/firmware.bin";
constexpr uint32_t kWifiConnectTimeoutMs = 15000;

}  // namespace

bool WifiSync::connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(kWifiSsid, kWifiPassword);

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
