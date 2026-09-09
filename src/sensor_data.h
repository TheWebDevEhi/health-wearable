#pragma once

#include <cstdint>

// Latest known reading from each subsystem. Written only by the sensor task;
// read by the display and BLE tasks. See DEVELOPMENT.md "Shared data store".
struct SensorSnapshot {
    // MAX30102
    float heartRateBpm = 0.0f;
    float spo2Percent = 0.0f;
    bool ppgSignalValid = false;

    // MLX90614
    float skinTempC = 0.0f;

    // LIS3DH
    uint32_t stepCount = 0;
    bool activityMoving = false;
    bool fallDetected = false;

    // MAX17048
    float batteryPercent = 0.0f;
    bool batteryLow = false;

    uint32_t lastUpdateMs = 0;

    // False until the sensor task has completed its first full cycle. Every
    // field above defaults to 0, and 0 is a false alert trigger for several
    // of them (e.g. battery/temp read as critically low) — gates alert
    // detection and "--" placeholders during that startup window.
    bool dataValid = false;
};

// Call once from setup(), before any task touches the store.
void sensorDataInit();

// Thread-safe copy-in / copy-out accessors — see DEVELOPMENT.md before adding
// a different access pattern (e.g. returning a pointer/reference).
SensorSnapshot sensorDataGet();
void sensorDataSet(const SensorSnapshot &snapshot);

// True when any reading has crossed the limits in config.h (readme.md #4).
// Shared by the display task (to switch to the Alert screen) and the BLE
// task (to set the "Motion & Control" alert-flags characteristic) so the two
// don't drift out of sync on what counts as an alert.
bool sensorDataIsAlert(const SensorSnapshot &snapshot);
