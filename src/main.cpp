#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "config.h"
#include "sensor_data.h"
#include "settings_store.h"

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

SettingsStore g_settings;
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

// Polls for a BLE-triggered OTA request (settings command 0x04) and runs it.
// Deliberately its own task, not folded into bleTask or powerTask:
// WifiSync::startOtaUpdate() blocks for a Wi-Fi connect timeout plus however
// long the actual download takes, and it must never block bleTask (BLE
// notifications would stall) or powerTask (idle/sleep handling would stall).
void otaTask(void *) {
    for (;;) {
        if (g_ble.consumeOtaRequest()) {
            // An in-progress OTA must not be interrupted by deep sleep; this
            // only resets the idle timer once at the start, not throughout
            // the download — a very slow transfer could still in theory lose
            // the race against CONNECTED_IDLE_TIMEOUT_MS. Good enough until
            // there's a real OTA server to actually measure transfer time
            // against (readme.md #7).
            g_power.noteActivity();
            Serial.println("[OTA] update requested, starting...");
            bool ok = g_wifi.startOtaUpdate();
            Serial.printf("[OTA] update %s\n", ok ? "succeeded" : "failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);

    sensorDataInit();
    g_settings.begin();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS);

    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);

    // Display first, so init failures below have somewhere to be shown —
    // not just a Serial log nobody without a debug cable would ever see
    // (readme.md #11; this was a real end-to-end finding, not a
    // hypothetical: a miswired sensor used to fail completely silently).
    g_screen.begin();
    g_ui.begin(g_screen);
    g_statusLed.begin();

    String failedInit;
    auto checkInit = [&failedInit](const char *name, bool ok) {
        if (ok) {
            return;
        }
        Serial.printf("[INIT] %s failed to initialize\n", name);
        if (failedInit.length() > 0) {
            failedInit += ", ";
        }
        failedInit += name;
    };

    checkInit("PPG", g_ppg.begin(Wire));
    checkInit("Temp", g_temp.begin(Wire));
    checkInit("Motion", g_motion.begin(Wire));
    g_motion.configureDoubleTapWake();
    checkInit("Fuel gauge", g_fuel.begin(Wire));
    checkInit("Touch", g_touch.begin());

    g_ble.attachSettingsStore(g_settings);
    checkInit("BLE", g_ble.begin());
    g_wifi.attachSettingsStore(g_settings);
    g_power.begin();
    g_power.attachScreen(g_screen);
    g_power.attachBle(g_ble);

    if (failedInit.length() > 0) {
        g_ui.renderBootError(failedInit);
        delay(3000);  // one-time pause so there's actually time to read it
    }

    xTaskCreate(sensorTask, "sensor", TASK_STACK_SENSOR, nullptr, TASK_PRIORITY_SENSOR, nullptr);
    xTaskCreate(displayTask, "display", TASK_STACK_DISPLAY, nullptr, TASK_PRIORITY_DISPLAY, nullptr);
    xTaskCreate(bleTask, "ble", TASK_STACK_BLE, nullptr, TASK_PRIORITY_BLE, nullptr);
    xTaskCreate(powerTask, "power", TASK_STACK_POWER, nullptr, TASK_PRIORITY_POWER, nullptr);
    xTaskCreate(otaTask, "ota", TASK_STACK_OTA, nullptr, TASK_PRIORITY_OTA, nullptr);
}

void loop() {
    // All work happens in the FreeRTOS tasks created in setup().
    vTaskDelay(pdMS_TO_TICKS(1000));
}
