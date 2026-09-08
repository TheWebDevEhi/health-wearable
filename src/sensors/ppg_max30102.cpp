#include "ppg_max30102.h"

#include "../config.h"

bool PpgMax30102::begin(TwoWire &bus) {
    // TODO: initialize the SparkFun MAX3010x driver on I2C_ADDR_MAX30102,
    // and configure LED currents and sample averaging.
    (void)bus;
    return false;
}

void PpgMax30102::update(bool armMoving) {
    // TODO: pull a rolling PPG window, filter, run the peak/ratio HR/SpO2
    // calculation, and hold the last good value while armMoving is true.
    (void)armMoving;
}
