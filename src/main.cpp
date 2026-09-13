#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "config.h"
#include "sensor_data.h"
#include "settings_store.h"

#include "sensors/fuel_max17048.h"
#include "sensors/motion_lis3dsh.h"
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
MotionLis3dsh g_motion;
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

// Set once in setup() from each sensor's begin() result, read every tick by
// sensorTask() to skip update() for anything that never initialized —
// found the hard way, not preemptively: a disconnected Fuel gauge's
// update() was retrying I2C against it every single 100ms tick regardless,
// spamming the shared bus and the log with a failed transaction forever
// (readme.md #11, DEVELOPMENT.md). A sensor that never came up isn't going
// to start responding on a later tick just because time passed.
bool g_ppgOk = false;
bool g_tempOk = false;
bool g_motionOk = false;
bool g_fuelOk = false;

// Whether the wearer has dismissed the currently-active alert (tap or
// button while it's showing) and, if so, when — used to re-interrupt with
// the Alert screen again after ALERT_ACK_COOLDOWN_MS if the underlying
// condition is still active. Reset the moment the real condition clears,
// so a *future* alert isn't pre-dismissed by an old tap. See
// displayTask(), pollButtons(), and pollTouch().
bool g_alertAcknowledged = false;
uint32_t g_alertAckMs = 0;

// See DEVELOPMENT.md "Task model" for why each task takes this shape:
// sample/render on a fixed tick, publish through sensor_data.h, never block
// on another task.

void sensorTask(void *) {
    for (;;) {
        SensorSnapshot snapshot = sensorDataGet();

        if (g_motionOk) {
            g_motion.update();
        }
        if (g_ppgOk) {
            g_ppg.update(g_motion.isMoving());
        }
        if (g_tempOk) {
            g_temp.update();
        }
        if (g_fuelOk) {
            g_fuel.update();
        }

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
//
// alertShowing (true whenever the Alert screen is actually on-screen right
// now — see displayTask()) makes a press dismiss the alert instead of
// cycling navigation, so the button can't do both at once in the same
// press. Found necessary during real bring-up: without this, the Alert
// screen had no way to leave at all — it overrides the display for as
// long as the underlying condition holds, and neither button nor touch
// could get past it (see config.h's ALERT_ACK_COOLDOWN_MS comment).
void pollButtons(bool alertShowing) {
    static bool prevButton1 = HIGH;

    bool button1 = digitalRead(PIN_BUTTON_1);
    bool button1Pressed = (prevButton1 == HIGH) && (button1 == LOW);
    prevButton1 = button1;

    if (!button1Pressed) {
        return;
    }
    g_power.noteActivity();

    if (alertShowing) {
        g_alertAcknowledged = true;
        g_alertAckMs = millis();
        return;
    }

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
// alertActive (true whenever the Alert screen is actually on-screen right
// now — see displayTask()) makes a tap dismiss the alert instead of
// reaching the nav-bar jump or Settings-zone dispatch below. renderAlert()
// draws no nav bar and no Settings rows, so without this branch a tap
// landing where those rows/zones normally are would blindly cycle
// brightness or fire an OTA request against a screen the wearer can't
// see — a real gap found in an end-to-end review, not a hypothetical
// (see DEVELOPMENT.md). Dismissing (rather than staying a no-op, which is
// what this used to do) was added after real bring-up showed the Alert
// screen otherwise had no way to leave at all — see config.h's
// ALERT_ACK_COOLDOWN_MS comment for how it re-interrupts if the condition
// persists. Button navigation deliberately keeps working during an alert
// (g_userScreen's own comment) since it only ever changes *where the user
// will land once the alert clears*, not an immediate side effect like
// touch's Settings zone can.
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
        g_alertAcknowledged = true;
        g_alertAckMs = millis();
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
        bool alertCondition = sensorDataIsAlert(snapshot);

        if (!alertCondition) {
            // Real condition cleared — don't let this dismissal carry over
            // and pre-silence a *future*, unrelated alert.
            g_alertAcknowledged = false;
        } else if (g_alertAcknowledged && (millis() - g_alertAckMs >= ALERT_ACK_COOLDOWN_MS)) {
            // Still going after the cooldown — re-interrupt rather than
            // staying dismissed forever from one old tap.
            g_alertAcknowledged = false;
        }
        bool showAlert = alertCondition && !g_alertAcknowledged;

        pollButtons(showAlert);
        pollTouch(showAlert);

        g_ui.show(showAlert ? UiScreen::Alert : g_userScreen);
        g_ui.render(snapshot);

        // The LED reflects the real condition regardless of whether the
        // wearer dismissed the on-screen alert — dismissal only changes
        // what the display shows, not whether something is actually wrong.
        if (alertCondition) {
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

// Blocks until the touch panel is pressed, the button is pressed, or a
// timeout elapses — a broken, unwired, or simply not-yet-connected touch
// panel must not be able to hang boot forever waiting for a touch that will
// never come. The button alone didn't actually satisfy that (a real gap
// found during bring-up without a display connected at all: nothing to tap,
// and the button requires a person standing by to press it) — the timeout
// is the real "never hangs forever" guarantee this function's own comment
// already promised. Returns false if the button or the timeout won the race.
bool waitForTouchOrSkip() {
    constexpr uint32_t kWaitTimeoutMs = 10000;
    uint32_t startMs = millis();
    for (;;) {
        if (g_touch.pressed()) {
            return true;
        }
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            return false;
        }
        if (millis() - startMs >= kWaitTimeoutMs) {
            Serial.println("[INIT] Touch calibration timed out, skipping");
            return false;
        }
        delay(20);
    }
}

// Waits for the panel to report "not pressed" before the next calibration
// target is shown, with the same kind of timeout as waitForTouchOrSkip() —
// found the hard way (readme.md #11, DEVELOPMENT.md) that this loop had none
// originally: with no touch panel connected, the IRQ pin floats and
// g_touch.pressed() reads as a persistent, never-clearing "pressed," so an
// unguarded release-wait here hung setup() forever, silently, well past
// where waitForTouchOrSkip()'s own timeout already fixed the first hang.
void waitForRelease() {
    constexpr uint32_t kReleaseTimeoutMs = 3000;
    uint32_t startMs = millis();
    while (g_touch.pressed()) {
        if (millis() - startMs >= kReleaseTimeoutMs) {
            Serial.println("[INIT] Touch release wait timed out, continuing");
            return;
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
    Serial.println("[BOOT] applyStoredOrNewCalibration() entered");
    constexpr int16_t kScreenX0 = TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenY0 = TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenX1 = TFT_WIDTH - TOUCH_CAL_MARGIN;
    constexpr int16_t kScreenY1 = TFT_HEIGHT - TOUCH_CAL_MARGIN;

    if (g_settings.touchCalibrated()) {
        Serial.println("[BOOT] using stored calibration");
        uint16_t rawX0, rawY0, rawX1, rawY1;
        g_settings.touchCalibrationRaw(rawX0, rawY0, rawX1, rawY1);
        g_touch.setCalibration(rawX0, rawY0, kScreenX0, kScreenY0, rawX1, rawY1, kScreenX1, kScreenY1);
        return;
    }

    Serial.println("[BOOT] no stored calibration, rendering prompt 1/2...");
    g_ui.renderCalibrationPrompt(1, kScreenX0, kScreenY0);
    Serial.println("[BOOT] prompt 1/2 rendered, waiting for touch/button/timeout...");
    if (!waitForTouchOrSkip()) {
        Serial.println("[BOOT] calibration point 1 skipped");
        return;
    }
    Serial.println("[BOOT] calibration point 1 touched, reading raw...");
    uint16_t rawX0, rawY0;
    g_touch.readRaw(rawX0, rawY0);
    waitForRelease();

    Serial.println("[BOOT] rendering prompt 2/2...");
    g_ui.renderCalibrationPrompt(2, kScreenX1, kScreenY1);
    Serial.println("[BOOT] prompt 2/2 rendered, waiting for touch/button/timeout...");
    if (!waitForTouchOrSkip()) {
        Serial.println("[BOOT] calibration point 2 skipped");
        return;
    }
    uint16_t rawX1, rawY1;
    g_touch.readRaw(rawX1, rawY1);
    waitForRelease();

    g_settings.setTouchCalibration(rawX0, rawY0, rawX1, rawY1);
    g_touch.setCalibration(rawX0, rawY0, kScreenX0, kScreenY0, rawX1, rawY1, kScreenX1, kScreenY1);
}

}  // namespace

void setup() {
    Serial.begin(115200);

    sensorDataInit();
    g_settings.begin();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // TEMPORARY bring-up diagnostic (readme.md #11): three of the four I2C
    // sensors were timing out on real hardware while one (PPG) worked fine,
    // which proves the bus itself is electrically sound and points at
    // per-device wiring/power/address rather than the bus. A raw scan
    // settles that in one boot instead of guessing address-by-address.
    // Remove once all four sensors are confirmed initializing.
    Serial.println("[I2C SCAN] Scanning 0x01-0x7E...");
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C SCAN] Found device at 0x%02X\n", addr);
        }
    }
    Serial.println("[I2C SCAN] Done.");

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

    g_ppgOk = g_ppg.begin(Wire);
    checkInit("PPG", g_ppgOk);
    g_tempOk = g_temp.begin(Wire);
    checkInit("Temp", g_tempOk);
    g_motionOk = g_motion.begin(Wire);
    checkInit("Motion", g_motionOk);
    // No configureDoubleTapWake() call — deliberately deferred for the
    // LIS3DSH (see motion_lis3dsh.h's class comment and power_mgr.cpp's
    // wake-source comment for the current consequence).
    g_fuelOk = g_fuel.begin(Wire);
    // Deliberately NOT checkInit() — the fuel gauge isn't wired up yet on
    // this specific unit, by choice, during bring-up (readme.md #11,
    // DEVELOPMENT.md), not an unexpected failure. checkInit() would put it
    // on the on-device orange boot-error screen and add a 3s pause on
    // every single boot for something already known and not actionable
    // right now. Still logged, so it's not silently invisible — just not
    // interrupting boot.
    if (!g_fuelOk) {
        Serial.println("[INIT] Fuel gauge failed to initialize (not shown on boot screen — not yet connected)");
    }
    bool touchOk = g_touch.begin();
    checkInit("Touch", touchOk);

    g_ble.attachSettingsStore(g_settings);
    checkInit("BLE", g_ble.begin());
    Serial.println("[BOOT] BLE begin() returned");
    g_wifi.attachSettingsStore(g_settings);
    g_power.begin();
    g_power.attachScreen(g_screen);
    g_power.attachBle(g_ble);
    Serial.println("[BOOT] power_mgr wired up");

    if (failedInit.length() > 0) {
        Serial.println("[BOOT] rendering boot-error screen...");
        g_ui.renderBootError(failedInit);
        Serial.println("[BOOT] boot-error screen rendered, pausing 3s");
        delay(3000);  // one-time pause so there's actually time to read it
        Serial.println("[BOOT] boot-error pause done");
    }

    // Only if Touch itself came up — calibration's wait loop needs a
    // working touch panel to ever return, and the button-skip escape
    // hatch only saves it from hanging, not from being pointless.
    Serial.printf("[BOOT] touchOk=%d, %s calibration\n", touchOk, touchOk ? "entering" : "skipping");
    if (touchOk) {
        applyStoredOrNewCalibration();
    }
    Serial.println("[BOOT] calibration step done, creating tasks");

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
