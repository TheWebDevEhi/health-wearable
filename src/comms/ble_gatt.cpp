#include "ble_gatt.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../config.h"
#include "../settings_store.h"

namespace {

// IEEE-11073 32-bit FLOAT: top byte is an 8-bit two's-complement exponent,
// low 3 bytes are a 24-bit two's-complement mantissa; value = mantissa *
// 10^exponent. Required wire format for the Health Thermometer
// characteristic (0x2A1C) — not a plain IEEE754 float.
int32_t encodeIeee11073Float(float value, int8_t exponent) {
    float scale = powf(10.0f, static_cast<float>(exponent));
    int32_t mantissa = static_cast<int32_t>(lroundf(value / scale));
    mantissa = std::max(-8388608, std::min(mantissa, 8388607));  // clamp to 24-bit signed
    return (static_cast<int32_t>(static_cast<uint8_t>(exponent)) << 24) | (mantissa & 0x00FFFFFF);
}

// Settings characteristic wire format — see DEVELOPMENT.md "Settings wire
// protocol" for the full spec and the matching companion-app encoder.
constexpr uint8_t kCmdSetDeviceName = 0x01;
constexpr uint8_t kCmdSetWifiCredentials = 0x02;
constexpr uint8_t kCmdRequestHistory = 0x03;
constexpr uint8_t kCmdStartOta = 0x04;
constexpr size_t kMaxDeviceNameLen = 20;
constexpr size_t kMaxSsidLen = 32;
constexpr size_t kMaxPasswordLen = 64;

}  // namespace

void BleGatt::ServerCallbacks::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
    (void)server;
    (void)connInfo;
    // Runs on NimBLE's own host task, not bleTask — _connectedCount is also
    // read from there (via clientConnected()) and from powerTask, so it's
    // mutex-guarded like everything else in this class touched from more
    // than one task (see the class comment in ble_gatt.h).
    if (xSemaphoreTake(_owner._mutex, portMAX_DELAY) == pdTRUE) {
        _owner._connectedCount++;
        xSemaphoreGive(_owner._mutex);
    }
    // History is NOT sent here — see the class comment in ble_gatt.h for
    // why (this used to be sendHistoryBacklog(), which raced the client's
    // own GATT subscription and silently lost the notifications). The
    // client requests it explicitly once ready.
}

void BleGatt::ServerCallbacks::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
    (void)server;
    (void)connInfo;
    (void)reason;
    if (xSemaphoreTake(_owner._mutex, portMAX_DELAY) == pdTRUE) {
        if (_owner._connectedCount > 0) {
            _owner._connectedCount--;
        }
        xSemaphoreGive(_owner._mutex);
    }
    // NimBLE stops advertising once connected; readme.md #7 requires a
    // fresh user tap to reconnect, but the peripheral still needs to be
    // advertising for that tap to find it.
    NimBLEDevice::startAdvertising();
}

void BleGatt::SettingsCallbacks::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
    (void)connInfo;
    std::string value = characteristic->getValue();
    if (value.empty()) {
        return;
    }

    uint8_t command = static_cast<uint8_t>(value[0]);
    if (command == kCmdSetDeviceName) {
        std::string name = value.substr(1);
        if (name.empty() || name.size() > kMaxDeviceNameLen) {
            Serial.println("[BLE] settings: invalid device name length");
            return;
        }
        _owner.applyDeviceName(String(name.c_str()));
        return;
    }

    if (command == kCmdSetWifiCredentials) {
        // [0]=command [1]=ssidLen [2..2+ssidLen)=ssid [..]=passLen [..]=password
        if (value.size() < 3) {
            Serial.println("[BLE] settings: malformed Wi-Fi credentials write");
            return;
        }
        size_t pos = 1;
        uint8_t ssidLen = static_cast<uint8_t>(value[pos]);
        pos += 1;
        if (ssidLen > kMaxSsidLen || value.size() < pos + ssidLen + 1) {
            Serial.println("[BLE] settings: malformed Wi-Fi credentials write");
            return;
        }
        std::string ssid = value.substr(pos, ssidLen);
        pos += ssidLen;

        uint8_t passLen = static_cast<uint8_t>(value[pos]);
        pos += 1;
        if (passLen > kMaxPasswordLen || value.size() < pos + passLen) {
            Serial.println("[BLE] settings: malformed Wi-Fi credentials write");
            return;
        }
        std::string password = value.substr(pos, passLen);

        _owner.applyWifiCredentials(String(ssid.c_str()), String(password.c_str()));
        return;
    }

    if (command == kCmdRequestHistory) {
        _owner.sendHistoryBacklog();
        return;
    }

    if (command == kCmdStartOta) {
        _owner.requestOta();
        return;
    }

    Serial.printf("[BLE] settings: unknown command 0x%02X\n", command);
}

void BleGatt::attachSettingsStore(SettingsStore &settings) {
    _settings = &settings;
}

bool BleGatt::begin() {
    _mutex = xSemaphoreCreateMutex();
    _bootMs = millis();

    String initialName = (_settings != nullptr) ? _settings->deviceName() : String(DEFAULT_DEVICE_NAME);

    NimBLEDevice::init(initialName.c_str());
    // Wi-Fi credentials (up to 32+64 bytes) plus the command/length header
    // exceed the default 23-byte ATT MTU; request a larger one so the
    // settings characteristic write in one piece rather than needing
    // reassembly this code doesn't implement.
    NimBLEDevice::setMTU(247);
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&_serverCallbacks);
    _server = server;

    // NimBLE-Arduino 2.x starts services automatically when the server
    // starts; the old explicit NimBLEService::start() call is deprecated.
    NimBLEService *hrService = server->createService("180D");
    _heartRateChar = hrService->createCharacteristic("2A37", NIMBLE_PROPERTY::NOTIFY);

    NimBLEService *thermService = server->createService("1809");
    _tempChar = thermService->createCharacteristic("2A1C", NIMBLE_PROPERTY::NOTIFY);

    NimBLEService *battService = server->createService("180F");
    _batteryChar = battService->createCharacteristic("2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    // Custom "Motion & Control" — UUIDs are project-defined, randomly
    // generated once (readme.md #7); keep them stable once the companion
    // PWA is built against them (see DEVELOPMENT.md).
    NimBLEService *motionService = server->createService("157a8ee9-7a76-4d8f-9fcf-b7d815451acc");
    // SpO2 has no BLE SIG characteristic in the services above — the
    // standard Pulse Oximeter Service (0x1822) exists but its wire format
    // is a heavier IEEE-11073 multi-field structure; a plain uint8 percent
    // here (same shape as Battery Level) is enough for this project. See
    // DEVELOPMENT.md "BLE UUIDs".
    _spo2Char = motionService->createCharacteristic("a8fb0a1c-eb12-47a6-8ffb-d65d1dc4eaeb",
                                                      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _stepsChar = motionService->createCharacteristic("71682870-7fc9-4624-811d-b3de04a1731f",
                                                       NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _flagsChar = motionService->createCharacteristic("0f0218c8-c147-4081-a7af-a9e539a3b5a6",
                                                       NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    _historyChar =
        motionService->createCharacteristic("a565c5f9-ce1e-42b3-95f6-b54504ad1286", NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *settingsChar = motionService->createCharacteristic(
        "ddb216da-d1f7-4301-994b-112316a5d4fd", NIMBLE_PROPERTY::WRITE);
    settingsChar->setCallbacks(&_settingsCallbacks);

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setName(initialName.c_str());
    advertising->addServiceUUID(hrService->getUUID());
    advertising->addServiceUUID(motionService->getUUID());
    advertising->enableScanResponse(true);
    advertising->start();

    return true;
}

void BleGatt::notify(const SensorSnapshot &data) {
    if (_server == nullptr || !data.dataValid) {
        return;
    }

    bool connected = clientConnected();

    if (data.ppgSignalValid) {
        uint8_t hrBuf[2] = {0x00,
                             static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, data.heartRateBpm)))};
        _heartRateChar->setValue(hrBuf, sizeof(hrBuf));
        if (connected) {
            _heartRateChar->notify();
        }

        uint8_t spo2Level = static_cast<uint8_t>(std::max(0.0f, std::min(100.0f, data.spo2Percent)));
        _spo2Char->setValue(&spo2Level, 1);
        if (connected) {
            _spo2Char->notify();
        }
    }

    uint8_t tempBuf[5];
    tempBuf[0] = 0x00;  // Celsius, no timestamp/type fields
    int32_t rawTemp = encodeIeee11073Float(data.skinTempC, -1);
    memcpy(&tempBuf[1], &rawTemp, sizeof(rawTemp));
    _tempChar->setValue(tempBuf, sizeof(tempBuf));
    if (connected) {
        _tempChar->notify();
    }

    uint8_t battLevel = static_cast<uint8_t>(std::max(0.0f, std::min(100.0f, data.batteryPercent)));
    _batteryChar->setValue(&battLevel, 1);
    if (connected) {
        _batteryChar->notify();
    }

    uint32_t steps = data.stepCount;
    _stepsChar->setValue(reinterpret_cast<uint8_t *>(&steps), sizeof(steps));
    if (connected) {
        _stepsChar->notify();
    }

    uint8_t flags = 0;
    if (data.activityMoving) {
        flags |= 0x01;
    }
    if (data.fallDetected) {
        flags |= 0x02;
    }
    if (sensorDataIsAlert(data)) {
        flags |= 0x04;
    }
    if (data.batteryLow) {
        flags |= 0x08;
    }
    _flagsChar->setValue(&flags, 1);
    if (connected) {
        _flagsChar->notify();
    }

    pushHistory(data);
}

bool BleGatt::clientConnected() {
    bool connected = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        connected = _connectedCount > 0;
        xSemaphoreGive(_mutex);
    }
    return connected;
}

void BleGatt::pushHistory(const SensorSnapshot &data) {
    HistoryEntry entry;
    entry.ageMs = millis() - _bootMs;
    entry.heartRateBpmX10 = static_cast<int16_t>(lroundf(data.heartRateBpm * 10.0f));
    entry.spo2PercentX10 = static_cast<int16_t>(lroundf(data.spo2Percent * 10.0f));
    entry.skinTempCx10 = static_cast<int16_t>(lroundf(data.skinTempC * 10.0f));

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _history[_historyHead] = entry;
        _historyHead = (_historyHead + 1) % kHistoryCapacity;
        if (_historyCount < kHistoryCapacity) {
            _historyCount++;
        }
        xSemaphoreGive(_mutex);
    }
}

void BleGatt::sendHistoryBacklog() {
    if (_historyChar == nullptr) {
        return;
    }

    // Snapshot under the lock, then notify outside it — NimBLE's own
    // notify() calls can take a while (radio I/O), and holding our mutex
    // for that whole span would block pushHistory()/clientConnected() on
    // other tasks for no reason.
    HistoryEntry snapshot[kHistoryCapacity];
    int count = 0;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        count = _historyCount;
        int oldest = (_historyHead - _historyCount + kHistoryCapacity) % kHistoryCapacity;
        for (int i = 0; i < count; i++) {
            snapshot[i] = _history[(oldest + i) % kHistoryCapacity];
        }
        xSemaphoreGive(_mutex);
    }

    // TODO: fires every buffered entry back-to-back with no pacing or
    // client ack; verify against real Web Bluetooth behaviour at bring-up
    // and add throttling/indications if notifications get dropped
    // (readme.md #6).
    for (int i = 0; i < count; i++) {
        _historyChar->setValue(reinterpret_cast<uint8_t *>(&snapshot[i]), sizeof(HistoryEntry));
        _historyChar->notify();
    }
}

void BleGatt::applyDeviceName(const String &name) {
    if (_settings != nullptr) {
        _settings->setDeviceName(name);
    }
    NimBLEDevice::setDeviceName(name.c_str());
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setName(name.c_str());
    // Restart so the new name actually shows up in scan results; harmless
    // to call stop() when advertising wasn't running.
    advertising->stop();
    advertising->start();
    Serial.printf("[BLE] device name changed to \"%s\"\n", name.c_str());
}

void BleGatt::applyWifiCredentials(const String &ssid, const String &password) {
    if (_settings != nullptr) {
        _settings->setWifiCredentials(ssid, password);
    }
    // Takes effect on the next OTA attempt — WifiSync reads from
    // SettingsStore each time rather than caching, and Wi-Fi is only
    // brought up on demand (readme.md #7), so there's nothing to reconnect
    // here.
    Serial.println("[BLE] Wi-Fi credentials updated");
}

void BleGatt::requestOta() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _otaRequested = true;
        xSemaphoreGive(_mutex);
    }
    Serial.println("[BLE] OTA update requested");
}

bool BleGatt::consumeOtaRequest() {
    bool requested = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        requested = _otaRequested;
        _otaRequested = false;
        xSemaphoreGive(_mutex);
    }
    return requested;
}
