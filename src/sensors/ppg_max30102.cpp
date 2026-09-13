#include "ppg_max30102.h"

#include <Arduino.h>
#include <spo2_algorithm.h>

#include "../config.h"

bool PpgMax30102::begin(TwoWire &bus)
{
    // SparkFun's begin() calls bus.setClock(i2cSpeed) — this reconfigures
    // the *shared* I2C bus for every sensor after this one, not just this
    // driver's own transactions. I2C_SPEED_FAST (400kHz) was a real,
    // confirmed bug on real hardware: the MLX90614 (temp_mlx90614.cpp)
    // doesn't reliably tolerate above its 100kHz standard-mode spec, so it
    // failed to initialize even though it was present and responding —
    // verified by scanning the bus before this call (found it at the right
    // address) vs. after (its own begin() failed the identical address
    // probe). I2C_SPEED_STANDARD keeps every sensor on this shared bus
    // within spec; nothing here needs 400kHz badly enough to risk it.
    if (!_sensor.begin(bus, I2C_SPEED_STANDARD))
    {
        return false;
    }

    // Defaults from the SparkFun SpO2 reference example; tune LED current
    // and sample rate once real skin-contact data is available (readme.md
    // #11).
    uint8_t ledBrightness = 25;
    uint8_t sampleAverage = 4;
    uint8_t ledMode = 2; // red + IR
    int sampleRate = 100;
    int pulseWidth = 215;
    int adcRange = 8192;
    _sensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    return true;
}

void PpgMax30102::update(bool armMoving)
{
    static uint32_t lastLogMs = 0; // TEMPORARY bring-up diagnostic (readme.md #11) — remove with the rest below
    if (millis() - lastLogMs >= 1000)
    {
        lastLogMs = millis();
        Serial.printf("[PPG DEBUG] armMoving=%d sampleIndex=%d irAvailable=%d\n", armMoving, _sampleIndex,
                      _sensor.available());
    }

    if (armMoving)
    {
        // Hold the last good value rather than showing a jumpy number
        // (readme.md #3); leave the rolling buffer alone while the arm is
        // moving too much to trust.
        return;
    }

    // Non-blocking drain: the reference SparkFun example busy-waits on
    // available(), but this runs on a shared FreeRTOS task tick, so it only
    // takes what's already in the FIFO this call instead of blocking for it.
    _sensor.check();
    while (_sensor.available() && _sampleIndex < kBufferLength)
    {
        _redBuffer[_sampleIndex] = _sensor.getRed();
        _irBuffer[_sampleIndex] = _sensor.getIR();
        _sensor.nextSample();
        _sampleIndex++;
    }

    if (_sampleIndex >= kBufferLength)
    {
        // TEMPORARY bring-up diagnostic (readme.md #11): a raw IR reading
        // confirms whether the sensor is even seeing a finger optically
        // (ambient light blocked, LED reflected back) before trusting the
        // algorithm's verdict on it. Remove once real HR/SpO2 confirmed.
        Serial.printf("[PPG DEBUG] buffer full, sample IR[0]=%lu IR[50]=%lu, running algorithm...\n",
                      static_cast<unsigned long>(_irBuffer[0]), static_cast<unsigned long>(_irBuffer[50]));
        runAlgorithm();
        _sampleIndex = 0;
    }
}

void PpgMax30102::runAlgorithm()
{
    int32_t spo2 = 0;
    int8_t spo2Valid = 0;
    int32_t heartRate = 0;
    int8_t heartRateValid = 0;

    maxim_heart_rate_and_oxygen_saturation(_irBuffer, kBufferLength, _redBuffer, &spo2,
                                           &spo2Valid, &heartRate, &heartRateValid);

    // TEMPORARY bring-up diagnostic (readme.md #11): shows both validity
    // flags separately (the app/BLE debug line only ever sees the combined
    // AND of the two) plus the raw computed values regardless of validity.
    // Remove once real HR/SpO2 confirmed reaching the app.
    Serial.printf("[PPG DEBUG] spo2=%ld spo2Valid=%d heartRate=%ld heartRateValid=%d\n",
                  static_cast<long>(spo2), spo2Valid, static_cast<long>(heartRate), heartRateValid);

    _signalValid = (spo2Valid == 1) && (heartRateValid == 1);
    if (_signalValid)
    {
        _spo2Percent = static_cast<float>(spo2);
        _heartRateBpm = static_cast<float>(heartRate);
    }
}
