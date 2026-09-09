#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "config.h"
#include "sensor_data.h"

#include "sensors/fuel_max17048.h"
#include "sensors/motion_lis3dh.h"
#include "sensors/ppg_max30102.h"
#include "sensors/temp_mlx90614.h"

#include "display/screen.h"
#include "display/status_led.h"
#include "display/touch_xpt2046.h"
#include "display/ui_screens.h"

#include "comms/ble_gatt.h"
#include "comms/wifi_sync.h"

#include "power/power_mgr.h"

namespace {

PpgMax30102 g_ppg;
TempMlx90614 g_temp;
MotionLis3dh g_motion;
FuelMax17048 g_fuel;

Screen g_screen;
TouchXpt2046 g_touch;
UiScreens g_ui;
StatusLed g_statusLed;

BleGatt g_ble;
WifiSync g_wifi;
PowerMgr g_power;

// The screen the user last navigated to, distinct from what's actually on
// screen right now — an active alert overrides the display without losing
// the user's place (see displayTask()).
UiScreen g_userScreen = UiScreen::Home;

// See DEVELOPMENT.md "Task model" for why each task takes this shape:
// sample/render on a fixed tick, publish through sensor_data.h, never block
// on another task.

void sensorTask(void *) {
    for (;;) {
        SensorSnapshot snapshot = sensorDataGet();

        g_motion.update();
        g_ppg.update(g_motion.isMoving());
        g_temp.update();
        g_fuel.update();

        snapshot.heartRateBpm = g_ppg.heartRateBpm();
        snapshot.spo2Percent = g_ppg.spo2Percent();
        snapshot.ppgSignalValid = g_ppg.signalValid();
        snapshot.skinTempC = g_temp.skinTempC();
        snapshot.stepCount = g_motion.stepCount();
        snapshot.activityMoving = g_motion.isMoving();
        snapshot.fallDetected = g_motion.fallDetected();
        snapshot.batteryPercent = g_fuel.batteryPercent();
        snapshot.batteryLow = g_fuel.batteryLow();
        snapshot.lastUpdateMs = millis();
        snapshot.dataValid = true;

        sensorDataSet(snapshot);
        vTaskDelay(pdMS_TO_TICKS(100));  // TODO tune per readme.md #4 sample rates
    }
}

// Button semantics (implementation choice, not specified in readme.md —
// confirm/tune at bring-up): Button 1 cycles Home -> Detail -> Settings ->
// Home; Button 2 cycles the Detail screen's metric, or jumps to Home from
// anywhere else. Polled at the display task's 200 ms tick rather than via
// interrupt, so a press shorter than that can be missed — fine for now, see
// DEVELOPMENT.md if it feels laggy on real hardware.
void pollButtons() {
    static bool prevButton1 = HIGH;
    static bool prevButton2 = HIGH;

    bool button1 = digitalRead(PIN_BUTTON_1);
    bool button2 = digitalRead(PIN_BUTTON_2);

    bool button1Pressed = (prevButton1 == HIGH) && (button1 == LOW);
    bool button2Pressed = (prevButton2 == HIGH) && (button2 == LOW);
    prevButton1 = button1;
    prevButton2 = button2;

    if (!button1Pressed && !button2Pressed) {
        return;
    }
    g_power.noteActivity();

    if (button1Pressed) {
        switch (g_userScreen) {
            case UiScreen::Home:
                g_userScreen = UiScreen::Detail;
                break;
            case UiScreen::Detail:
                g_userScreen = UiScreen::Settings;
                break;
            case UiScreen::Settings:
            case UiScreen::Alert:
                g_userScreen = UiScreen::Home;
                break;
        }
    }
    if (button2Pressed) {
        if (g_userScreen == UiScreen::Detail) {
            g_ui.nextDetailMetric();
        } else {
            g_userScreen = UiScreen::Home;
        }
    }
}

void displayTask(void *) {
    for (;;) {
        pollButtons();

        SensorSnapshot snapshot = sensorDataGet();
        bool alert = sensorDataIsAlert(snapshot);

        g_ui.show(alert ? UiScreen::Alert : g_userScreen);
        g_ui.render(snapshot);

        if (alert) {
            g_statusLed.setAlert();
        } else {
            g_statusLed.setOk();
        }
        g_statusLed.tick();

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void bleTask(void *) {
    for (;;) {
        g_ble.notify(sensorDataGet());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void powerTask(void *) {
    for (;;) {
        g_power.update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);

    sensorDataInit();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS);

    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);

    g_ppg.begin(Wire);
    g_temp.begin(Wire);
    g_motion.begin(Wire);
    g_motion.configureDoubleTapWake();
    g_fuel.begin(Wire);

    g_screen.begin();
    g_touch.begin();
    g_ui.begin(g_screen);
    g_statusLed.begin();

    g_ble.begin();
    g_power.begin();
    g_power.attachScreen(g_screen);

    xTaskCreate(sensorTask, "sensor", TASK_STACK_SENSOR, nullptr, TASK_PRIORITY_SENSOR, nullptr);
    xTaskCreate(displayTask, "display", TASK_STACK_DISPLAY, nullptr, TASK_PRIORITY_DISPLAY, nullptr);
    xTaskCreate(bleTask, "ble", TASK_STACK_BLE, nullptr, TASK_PRIORITY_BLE, nullptr);
    xTaskCreate(powerTask, "power", TASK_STACK_POWER, nullptr, TASK_PRIORITY_POWER, nullptr);
}

void loop() {
    // All work happens in the FreeRTOS tasks created in setup().
    vTaskDelay(pdMS_TO_TICKS(1000));
}
