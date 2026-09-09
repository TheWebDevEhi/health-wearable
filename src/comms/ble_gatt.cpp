#include "ble_gatt.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>

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

}  // namespace

void BleGatt::ServerCallbacks::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
    (void)server;
    (void)connInfo;
    _owner._connectedCount++;
    _owner.sendHistoryBacklog();
}

void BleGatt::ServerCallbacks::onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) {
    (void)server;
    (void)connInfo;
    (void)reason;
    if (_owner._connectedCount > 0) {
        _owner._connectedCount--;
    }
    // NimBLE stops advertising once connected; readme.md #7 requires a
    // fresh user tap to reconnect, but the peripheral still needs to be
    // advertising for that tap to find it.
    NimBLEDevice::startAdvertising();
}

void BleGatt::SettingsCallbacks::onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) {
    (void)connInfo;
    // TODO: no settings wire format is defined yet (readme.md #4 lists
    // brightness/alert limits as adjustable, but not their BLE encoding) —
    // this only logs what arrived until that's designed.
    std::string value = characteristic->getValue();
    Serial.printf("[BLE] settings write, %u bytes\n", static_cast<unsigned>(value.size()));
}

bool BleGatt::begin() {
    _bootMs = millis();

    NimBLEDevice::init("BEME Band");
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
    advertising->setName("BEME Band");
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

void BleGatt::pushHistory(const SensorSnapshot &data) {
    HistoryEntry entry;
    entry.ageMs = millis() - _bootMs;
    entry.heartRateBpmX10 = static_cast<int16_t>(lroundf(data.heartRateBpm * 10.0f));
    entry.spo2PercentX10 = static_cast<int16_t>(lroundf(data.spo2Percent * 10.0f));
    entry.skinTempCx10 = static_cast<int16_t>(lroundf(data.skinTempC * 10.0f));

    _history[_historyHead] = entry;
    _historyHead = (_historyHead + 1) % kHistoryCapacity;
    if (_historyCount < kHistoryCapacity) {
        _historyCount++;
    }
}

void BleGatt::sendHistoryBacklog() {
    if (_historyChar == nullptr || _historyCount == 0) {
        return;
    }

    // TODO: fires every buffered entry back-to-back with no pacing or
    // client ack; verify against real Web Bluetooth behaviour at bring-up
    // and add throttling/indications if notifications get dropped
    // (readme.md #6).
    int oldest = (_historyHead - _historyCount + kHistoryCapacity) % kHistoryCapacity;
    for (int i = 0; i < _historyCount; i++) {
        int idx = (oldest + i) % kHistoryCapacity;
        _historyChar->setValue(reinterpret_cast<uint8_t *>(&_history[idx]), sizeof(HistoryEntry));
        _historyChar->notify();
    }
}
