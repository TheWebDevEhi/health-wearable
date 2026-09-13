#pragma once

#include "../sensor_data.h"
#include "screen.h"

// The four screens from readme.md #4: Home, Detail, Alert, Settings.
enum class UiScreen {
    Home,
    Detail,
    Alert,
    Settings,
};

// Which value the Detail screen is currently showing; cycled by tapping
// the Detail content area (see main.cpp's pollTouch()).
enum class DetailMetric {
    HeartRate,
    Spo2,
    Temperature,
    Steps,
    Battery,
};

// Which row of the Settings screen a tap landed on — None for the
// alert-limits row, which is display-only (readme.md #5, phone-app-only by
// design). See UiScreens::settingsZoneAt().
enum class SettingsZone {
    None,
    Brightness,
    SyncNow,
};

// Owns which screen is showing and redraws on each render() call; navigated
// with the single physical button (full-screen cycling) and the touch nav
// bar (direct jump + Detail-metric cycling) — readme.md #5. TODO: skip the
// redraw when nothing changed, once flicker/refresh cost is measured on
// real hardware.
class UiScreens {
public:
    void begin(Screen &screen);

    void show(UiScreen screen);
    UiScreen current() const { return _current; }

    void nextDetailMetric();

    // Mirrors SettingsStore::brightnessLevel() so the Settings screen has
    // something to display — main.cpp is the coordinator that keeps this
    // in sync with the persisted value (readme.md #5, DEVELOPMENT.md).
    void setBrightnessLevel(uint8_t level) { _brightnessLevel = level; }

    void render(const SensorSnapshot &data);

    // One-shot screen for setup() to call directly if a sensor failed to
    // initialize — outside the normal Home/Detail/Alert/Settings state
    // machine, since it runs before any task (and thus any SensorSnapshot)
    // exists. Added so a miswired sensor doesn't fail completely silently
    // (readme.md #11) — previously nothing surfaced this beyond a Serial
    // log nobody without a debug cable would ever see.
    void renderBootError(const String &failedList);

    // Another one-shot, boot-time screen: draws a crosshair target at
    // (x, y) for the touch calibration flow in main.cpp to wait on.
    // pointNumber is 1 or 2, just for the on-screen "n/2" label.
    void renderCalibrationPrompt(int pointNumber, int16_t x, int16_t y);

    // Nav-bar hit test: given a touch's screen X (the caller has already
    // checked the Y falls within the nav bar's band), returns which screen
    // that tap should jump to. Kept here rather than duplicated in
    // main.cpp's pollTouch(), so the tap zones can never drift out of sync
    // with drawNavBar()'s actual drawn layout.
    static UiScreen navZoneAt(int16_t x);

    // Same idea as navZoneAt(), for the Settings screen's rows instead of
    // the nav bar's columns.
    static SettingsZone settingsZoneAt(int16_t y);

private:
    static constexpr int kTrendPoints = 40;

    Screen *_screen = nullptr;
    UiScreen _current = UiScreen::Home;
    DetailMetric _detailMetric = DetailMetric::HeartRate;
    uint8_t _brightnessLevel = 0;

    float _trend[kTrendPoints] = {};
    int _trendIndex = 0;
    int _trendCount = 0;

    void renderHome(const SensorSnapshot &data);
    void renderDetail(const SensorSnapshot &data);
    void renderAlert(const SensorSnapshot &data);
    void renderSettings();
    // Not drawn on Alert — that screen stays a full-screen warning.
    void drawNavBar();

    void pushTrendSample(float value);
    void drawTrend(int x, int y, int w, int h);
    float detailValue(const SensorSnapshot &data) const;
    const char *detailLabel() const;
};
