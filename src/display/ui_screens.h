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

// Which value the Detail screen is currently showing; cycled by a button
// press (see main.cpp).
enum class DetailMetric {
    HeartRate,
    Spo2,
    Temperature,
    Steps,
    Battery,
};

// Owns which screen is showing and redraws on each render() call; navigated
// with the two side buttons (readme.md #4). TODO: skip the redraw when
// nothing changed, once flicker/refresh cost is measured on real hardware.
class UiScreens {
public:
    void begin(Screen &screen);

    void show(UiScreen screen);
    UiScreen current() const { return _current; }

    void nextDetailMetric();

    void render(const SensorSnapshot &data);

    // One-shot screen for setup() to call directly if a sensor failed to
    // initialize — outside the normal Home/Detail/Alert/Settings state
    // machine, since it runs before any task (and thus any SensorSnapshot)
    // exists. Added so a miswired sensor doesn't fail completely silently
    // (readme.md #11) — previously nothing surfaced this beyond a Serial
    // log nobody without a debug cable would ever see.
    void renderBootError(const String &failedList);

private:
    static constexpr int kTrendPoints = 40;

    Screen *_screen = nullptr;
    UiScreen _current = UiScreen::Home;
    DetailMetric _detailMetric = DetailMetric::HeartRate;

    float _trend[kTrendPoints] = {};
    int _trendIndex = 0;
    int _trendCount = 0;

    void renderHome(const SensorSnapshot &data);
    void renderDetail(const SensorSnapshot &data);
    void renderAlert(const SensorSnapshot &data);
    void renderSettings();

    void pushTrendSample(float value);
    void drawTrend(int x, int y, int w, int h);
    float detailValue(const SensorSnapshot &data) const;
    const char *detailLabel() const;
};
