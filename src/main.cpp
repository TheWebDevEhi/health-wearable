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
// confirm/tune at bring-up): the single button cycles Home -> Detail ->
// Settings -> Home. Polled at the display task's 200 ms tick rather than
// via interrupt, so a press shorter than that can be missed — fine for now,
// see DEVELOPMENT.md if it feels laggy on real hardware.
void pollButtons() {
    static bool prevButton1 = HIGH;

    bool button1 = digitalRead(PIN_BUTTON_1);
    bool button1Pressed = (prevButton1 == HIGH) && (button1 == LOW);
    prevButton1 = button1;

    if (!button1Pressed) {
        return;
    }
    g_power.noteActivity();

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

// Applies a brightness level everywhere it needs to land: the actual PWM
// (Screen), what the Settings screen displays (UiScreens), and — only when
// persist is true — NVS (SettingsStore). persist=false is for applying the
// already-persisted level once at boot, where writing it straight back to
// NVS would just be a pointless flash write.
void applyBrightnessLevel(uint8_t level, bool persist) {
    g_screen.setBacklight(BRIGHTNESS_PERCENTS[level]);
    g_ui.setBrightnessLevel(level);
    if (persist) {
        g_settings.setBrightnessLevel(level);
    }
}

// Touch semantics (readme.md #5): tapping the bottom nav bar jumps
// directly to that screen. Above the bar, what a tap does depends on the
// screen: on Detail it cycles the shown metric (the same action the
// now-removed Button 2 used to perform); on Settings, its row decides —
// Brightness cycles through BRIGHTNESS_PERCENTS, Sync Now requests an OTA
// check the same way a BLE kCmdStartOta write would (readme.md #7); the
// alert-limits row is display-only (phone-app-only, by design). Home has
// no content-area action. Edge-triggered on the rising press (like
// pollButtons()) so holding a finger down doesn't repeat every 200 ms
// tick. A no-op until g_touch has been calibrated (readCalibrated()
// always returns false until then — see applyStoredOrNewCalibration()).
//
// alertActive silences the nav-bar jump and Settings-zone dispatch (but
// not activity-tracking — see below) while the Alert screen is showing.
// renderAlert() draws no nav bar and no Settings rows, so without this a
// tap landing where those rows/zones normally are would blindly cycle
// brightness or fire an OTA request against a screen the wearer can't
// see — a real gap found in an end-to-end review, not a hypothetical
// (see DEVELOPMENT.md). Button navigation deliberately keeps working
// during an alert (g_userScreen's own comment) since it only ever changes
// *where the user will land once the alert clears*, not an immediate
// side effect like touch's Settings zone can.
void pollTouch(bool alertActive) {
    static bool prevTouched = false;

    int16_t x, y;
    bool touched = g_touch.readCalibrated(x, y);
    bool justPressed = touched && !prevTouched;
    prevTouched = touched;

    if (!justPressed) {
        return;
    }
    // A tap is real user activity regardless of what it's allowed to do —
    // counts toward the idle/sleep timer even when alertActive suppresses
    // everything below.
    g_power.noteActivity();
    if (alertActive) {
        return;
    }

    const int navBarTop = TFT_HEIGHT - NAV_BAR_HEIGHT;
    if (y >= navBarTop) {
        g_userScreen = UiScreens::navZoneAt(x);
        return;
    }

    if (g_userScreen == UiScreen::Detail) {
        g_ui.nextDetailMetric();
        return;
    }

    if (g_userScreen == UiScreen::Settings) {
        switch (UiScreens::settingsZoneAt(y)) {
            case SettingsZone::Brightness: {
                uint8_t next = (g_settings.brightnessLevel() + 1) % BRIGHTNESS_LEVEL_COUNT;
                applyBrightnessLevel(next, /*persist=*/true);
                break;
            }
            case SettingsZone::SyncNow:
                g_ble.requestOta();
                break;
            case SettingsZone::None:
                break;
        }
    }
}

void displayTask(void *) {
    for (;;) {
        SensorSnapshot snapshot = sensorDataGet();
        bool alert = sensorDataIsAlert(snapshot);

        pollButtons();
        pollTouch(alert);

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
        // Applies a queued device-rename's advertising restart here,
        // deliberately not inside the GATT write callback that requested
        // it — see BleGatt::applyDeviceName()'s comment.
        g_ble.pollPendingNameChange();
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

// Blocks until the touch panel is pressed, or the button is pressed as an
// escape hatch — a broken or unwired touch panel must not be able to hang
// boot forever waiting for a touch that will never come. Returns false if
// the button won the race.
bool waitForTouchOrSkip() {
    for (;;) {
        if (g_touch.pressed()) {
            return true;
        }
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            return false;
        }
        delay(20);
    }
}

// One-time, two-point touch calibration (readme.md #5, #11). Runs from
// setup(), before any task exists, so blocking here is fine — it's the
// same shape as the existing boot-error pause below. Only reached if Touch
// itself initialized OK (see the touchOk check in setup()); if it's never
// calibrated (skipped via the button, or Touch failed to init), g_touch's
// readCalibrated() just keeps returning false forever and pollTouch()
// stays a permanent no-op — full-screen navigation still works via the
// physical button either way.
void applyStoredOrNewCalibration() {
    constexpr int16_t kScreenX0 = TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenY0 = TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenX1 = TFT_WIDTH - TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenY1 = TFT_HEIGHT - TOUCH_CAL_MARGIN;

    if (g_settings.touchCalibrated()) {
        uint16_t rawX0, rawY0, rawX1, rawY1;
        g_settings.touchCalibrationRaw(rawX0, rawY0, rawX1, rawY1);
        g_touch.setCalibration(rawX0, rawY0, kScreenX0, kScreenY0, rawX1, rawY1, kScreenX1, kScreenY1);
        return;
    }

    g_ui.renderCalibrationPrompt(1, kScreenX0, kScreenY0);
    if (!waitForTouchOrSkip()) {
        return;
    }
    uint16_t rawX0, rawY0;
    g_touch.readRaw(rawX0, rawY0);
    while (g_touch.pressed()) {
        delay(20);  // wait for release before showing the next target
    }

    g_ui.renderCalibrationPrompt(2, kScreenX1, kScreenY1);
    if (!waitForTouchOrSkip()) {
        return;
    }
    uint16_t rawX1, rawY1;
    g_touch.readRaw(rawX1, rawY1);
    while (g_touch.pressed()) {
        delay(20);
    }

    g_settings.setTouchCalibration(rawX0, rawY0, rawX1, rawY1);
    g_touch.setCalibration(rawX0, rawY0, kScreenX0, kScreenY0, rawX1, rawY1, kScreenX1, kScreenY1);
}

}  // namespace

void setup() {
    Serial.begin(115200);

    sensorDataInit();
    g_settings.begin();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_TFT_CS);

    pinMode(PIN_BUTTON_1, INPUT_PULLUP);

    // Display first, so init failures below have somewhere to be shown —
    // not just a Serial log nobody without a debug cable would ever see
    // (readme.md #11; this was a real end-to-end finding, not a
    // hypothetical: a miswired sensor used to fail completely silently).
    g_screen.begin();
    g_ui.begin(g_screen);
    g_statusLed.begin();
    // Apply whatever brightness level was persisted from a previous
    // boot, overriding Screen::begin()'s fully-on default. Not persisting
    // it right back — it's already what's in NVS.
    applyBrightnessLevel(g_settings.brightnessLevel(), /*persist=*/false);

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
    bool touchOk = g_touch.begin();
    checkInit("Touch", touchOk);

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

    // Only if Touch itself came up — calibration's wait loop needs a
    // working touch panel to ever return, and the button-skip escape
    // hatch only saves it from hanging, not from being pointless.
    if (touchOk) {
        applyStoredOrNewCalibration();
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
