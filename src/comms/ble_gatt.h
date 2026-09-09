#pragma once

#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../sensor_data.h"

class SettingsStore;  // see attachSettingsStore()

// Standard + custom GATT services (readme.md #7): Heart Rate (0x180D),
// Health Thermometer (0x1809), Battery (0x180F), and a custom "Motion &
// Control" service (UUIDs defined in ble_gatt.cpp — project-defined,
// randomly generated, not from any BLE SIG spec; see DEVELOPMENT.md), plus a
// rolling history buffer. SpO2 also lives in the custom service (a plain
// uint8 percent characteristic) rather than the BLE SIG's Pulse Oximeter
// Service — see DEVELOPMENT.md "BLE UUIDs" for why. The settings
// characteristic accepts device-name, Wi-Fi-credential, request-history,
// and start-OTA writes — see DEVELOPMENT.md "Settings wire protocol" for
// the exact byte format.
//
// History is sent only when the client explicitly requests it (command
// 0x03), not automatically on connect — an earlier version pushed it from
// onConnect(), but that fires at BLE link-layer connect, before the client
// has finished GATT-subscribing to the history characteristic, so the
// notifications were being sent to no subscriber and silently dropped. The
// companion app requests history once it has finished subscribing to
// everything (see companion-app/js/ble.js).
//
// _mutex guards every member below that's touched from more than one
// FreeRTOS task: the history ring buffer, the connected-client count, and
// the OTA request flag are all written from NimBLE's own host task (inside
// ServerCallbacks/SettingsCallbacks, which run on connect/disconnect/write
// events) while also being read from bleTask (notify()) and, for
// clientConnected(), powerTask too. This used to be unsynchronized shared
// state — a real, if narrow-window, data race — found during an
// end-to-end review; see DEVELOPMENT.md's dated log entry for the details.
class BleGatt {
public:
    bool begin();

    // Must be called before begin() — the initial advertised name comes
    // from here.
    void attachSettingsStore(SettingsStore &settings);

    void notify(const SensorSnapshot &data);

    bool clientConnected();

    // Returns true once if a BLE client has requested an OTA update since
    // the last call, clearing the request (test-and-clear, so two pollers
    // could never both act on the same request). Poll this from a task
    // that can afford to block on Wi-Fi/HTTP — never from bleTask or a
    // NimBLE callback. See main.cpp's otaTask.
    bool consumeOtaRequest();

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
        explicit SettingsCallbacks(BleGatt &owner) : _owner(owner) {}
        void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override;

    private:
        BleGatt &_owner;
    };

    ServerCallbacks _serverCallbacks{*this};
    SettingsCallbacks _settingsCallbacks{*this};

    NimBLEServer *_server = nullptr;
    NimBLECharacteristic *_heartRateChar = nullptr;
    NimBLECharacteristic *_tempChar = nullptr;
    NimBLECharacteristic *_batteryChar = nullptr;
    NimBLECharacteristic *_spo2Char = nullptr;
    NimBLECharacteristic *_stepsChar = nullptr;
    NimBLECharacteristic *_flagsChar = nullptr;
    NimBLECharacteristic *_historyChar = nullptr;

    SettingsStore *_settings = nullptr;

    SemaphoreHandle_t _mutex = nullptr;

    // Everything from here down is only ever touched while holding _mutex.
    HistoryEntry _history[kHistoryCapacity] = {};
    int _historyHead = 0;
    int _historyCount = 0;
    int _connectedCount = 0;
    bool _otaRequested = false;

    uint32_t _bootMs = 0;  // set once in begin(), read-only after — no lock needed

    void pushHistory(const SensorSnapshot &data);
    void sendHistoryBacklog();
    void applyDeviceName(const String &name);
    void applyWifiCredentials(const String &ssid, const String &password);
    void requestOta();

    friend class ServerCallbacks;
    friend class SettingsCallbacks;
};
