#pragma once

#include <NimBLEDevice.h>

#include "../sensor_data.h"

// Standard + custom GATT services (readme.md #7): Heart Rate (0x180D),
// Health Thermometer (0x1809), Battery (0x180F), and a custom "Motion &
// Control" service (UUIDs defined in ble_gatt.cpp — project-defined,
// randomly generated, not from any BLE SIG spec; see DEVELOPMENT.md), plus a
// rolling history buffer sent on reconnect.
class BleGatt {
public:
    bool begin();
    void notify(const SensorSnapshot &data);

    bool clientConnected() const { return _connectedCount > 0; }

private:
    // ~1 minute of history at the BLE task's 1 Hz notify tick. TODO: size
    // against real reconnect gaps once the companion PWA exists.
    static constexpr int kHistoryCapacity = 60;

    // Fixed-point encoding to keep each entry small — this buffer lives in
    // RAM for the device's whole uptime.
    struct HistoryEntry {
        uint32_t ageMs;
        int16_t heartRateBpmX10;
        int16_t spo2PercentX10;
        int16_t skinTempCx10;
    } __attribute__((packed));

    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        explicit ServerCallbacks(BleGatt &owner) : _owner(owner) {}
        void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
        void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;

    private:
        BleGatt &_owner;
    };

    class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    public:
        void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;
    };

    ServerCallbacks _serverCallbacks{*this};
    SettingsCallbacks _settingsCallbacks;

    NimBLEServer *_server = nullptr;
    NimBLECharacteristic *_heartRateChar = nullptr;
    NimBLECharacteristic *_tempChar = nullptr;
    NimBLECharacteristic *_batteryChar = nullptr;
    NimBLECharacteristic *_stepsChar = nullptr;
    NimBLECharacteristic *_flagsChar = nullptr;
    NimBLECharacteristic *_historyChar = nullptr;

    HistoryEntry _history[kHistoryCapacity] = {};
    int _historyHead = 0;
    int _historyCount = 0;
    int _connectedCount = 0;
    uint32_t _bootMs = 0;

    void pushHistory(const SensorSnapshot &data);
    void sendHistoryBacklog();

    friend class ServerCallbacks;
};
