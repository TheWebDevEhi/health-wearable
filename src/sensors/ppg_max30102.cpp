#include "ppg_max30102.h"

#include <spo2_algorithm.h>

#include "../config.h"

bool PpgMax30102::begin(TwoWire &bus) {
    if (!_sensor.begin(bus, I2C_SPEED_FAST)) {
        return false;
    }

    // Defaults from the SparkFun SpO2 reference example; tune LED current
    // and sample rate once real skin-contact data is available (readme.md
    // #11).
    uint8_t ledBrightness = 60;
    uint8_t sampleAverage = 4;
    uint8_t ledMode = 2;  // red + IR
    int sampleRate = 100;
    int pulseWidth = 411;
    int adcRange = 4096;
    _sensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    return true;
}

void PpgMax30102::update(bool armMoving) {
    if (armMoving) {
        // Hold the last good value rather than showing a jumpy number
        // (readme.md #3); leave the rolling buffer alone while the arm is
        // moving too much to trust.
        return;
    }

    // Non-blocking drain: the reference SparkFun example busy-waits on
    // available(), but this runs on a shared FreeRTOS task tick, so it only
    // takes what's already in the FIFO this call instead of blocking for it.
    _sensor.check();
    while (_sensor.available() && _sampleIndex < kBufferLength) {
        _redBuffer[_sampleIndex] = _sensor.getRed();
        _irBuffer[_sampleIndex] = _sensor.getIR();
        _sensor.nextSample();
        _sampleIndex++;
    }

    if (_sampleIndex >= kBufferLength) {
        runAlgorithm();
        _sampleIndex = 0;
    }
}

void PpgMax30102::runAlgorithm() {
    int32_t spo2 = 0;
    int8_t spo2Valid = 0;
    int32_t heartRate = 0;
    int8_t heartRateValid = 0;

    maxim_heart_rate_and_oxygen_saturation(_irBuffer, kBufferLength, _redBuffer, &spo2,
                                            &spo2Valid, &heartRate, &heartRateValid);

    _signalValid = (spo2Valid == 1) && (heartRateValid == 1);
    if (_signalValid) {
        _spo2Percent = static_cast<float>(spo2);
        _heartRateBpm = static_cast<float>(heartRate);
    }
}
