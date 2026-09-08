#pragma once

#include "../sensor_data.h"

// Standard + custom GATT services (readme.md #7): Heart Rate (0x180D),
// Health Thermometer (0x1809), Battery (0x180F), and a custom "Motion &
// Control" service, plus the rolling history buffer sent on reconnect.
class BleGatt {
public:
    bool begin();
    void notify(const SensorSnapshot &data);

    bool clientConnected() const { return _connected; }

private:
    bool _connected = false;
};
