#include "sensor_data.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
