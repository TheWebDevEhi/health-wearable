#include "ble_gatt.h"

// TODO: NimBLE-Arduino server setup for the services listed in ble_gatt.h,
// plus the rolling history buffer that backfills the phone on reconnect
// (readme.md #7).

bool BleGatt::begin() {
    return false;
}

void BleGatt::notify(const SensorSnapshot &data) {
    (void)data;
}
