#include "sensor_data.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"

namespace {
SensorSnapshot g_snapshot;
SemaphoreHandle_t g_mutex = nullptr;
}  // namespace

void sensorDataInit() {
    g_mutex = xSemaphoreCreateMutex();
}

SensorSnapshot sensorDataGet() {
    SensorSnapshot copy;
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        copy = g_snapshot;
        xSemaphoreGive(g_mutex);
    }
    return copy;
}

void sensorDataSet(const SensorSnapshot &snapshot) {
    if (g_mutex != nullptr && xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        g_snapshot = snapshot;
        xSemaphoreGive(g_mutex);
    }
}

bool sensorDataIsAlert(const SensorSnapshot &snapshot) {
    if (!snapshot.dataValid) {
        return false;
    }
    if (snapshot.ppgSignalValid) {
        if (snapshot.heartRateBpm < ALERT_HR_LOW_BPM || snapshot.heartRateBpm > ALERT_HR_HIGH_BPM) {
            return true;
        }
        if (snapshot.spo2Percent < ALERT_SPO2_LOW_PCT) {
            return true;
        }
    }
    if (snapshot.skinTempC < ALERT_TEMP_LOW_C || snapshot.skinTempC > ALERT_TEMP_HIGH_C) {
        return true;
    }
    return snapshot.fallDetected || snapshot.batteryLow;
}
