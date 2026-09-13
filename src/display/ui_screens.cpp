#include "ui_screens.h"

#include <algorithm>

#include "../config.h"

namespace {
constexpr int kScreenW = TFT_WIDTH;

// Settings screen row layout — shared between renderSettings() (what's
// drawn) and settingsZoneAt() (what a tap there means), same reasoning as
// drawNavBar()/navZoneAt(): one definition each row can't drift out of
// sync with the other.
constexpr int kSettingsBrightnessRowY = 24;
constexpr int kSettingsSyncRowY = 44;
constexpr int kSettingsAlertRowY = 64;
constexpr int kSettingsRowTapHeight = 18;
}  // namespace

void UiScreens::begin(Screen &screen) {
    _screen = &screen;
}

void UiScreens::show(UiScreen screen) {
    _current = screen;
}

void UiScreens::nextDetailMetric() {
    switch (_detailMetric) {
        case DetailMetric::HeartRate:
            _detailMetric = DetailMetric::Spo2;
            break;
        case DetailMetric::Spo2:
            _detailMetric = DetailMetric::Temperature;
            break;
        case DetailMetric::Temperature:
            _detailMetric = DetailMetric::Steps;
            break;
        case DetailMetric::Steps:
            _detailMetric = DetailMetric::Battery;
            break;
        case DetailMetric::Battery:
            _detailMetric = DetailMetric::HeartRate;
            break;
    }
    // A metric switch makes the old trend meaningless.
    _trendIndex = 0;
    _trendCount = 0;
}

void UiScreens::render(const SensorSnapshot &data) {
    if (_screen == nullptr) {
        return;
    }
    switch (_current) {
        case UiScreen::Home:
            renderHome(data);
            break;
        case UiScreen::Detail:
            renderDetail(data);
            break;
        case UiScreen::Alert:
            renderAlert(data);
            break;
        case UiScreen::Settings:
            renderSettings();
            break;
    }
}

void UiScreens::renderHome(const SensorSnapshot &data) {
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setTextSize(1);
    tft.setCursor(4, 4);
    tft.print("HR");
    tft.setTextSize(3);
    tft.setCursor(4, 14);
    tft.print(data.ppgSignalValid ? String(data.heartRateBpm, 0) : String("--"));

    tft.setTextSize(1);
    tft.setCursor(4, 46);
    tft.print("SpO2");
    tft.setTextSize(3);
    tft.setCursor(4, 56);
    tft.print(data.ppgSignalValid ? (String(data.spo2Percent, 0) + "%") : String("--"));

    tft.setTextSize(1);
    tft.setCursor(4, 92);
    tft.printf("Temp: %.1f C", data.skinTempC);

    tft.setCursor(4, 106);
    tft.printf("Steps: %lu", static_cast<unsigned long>(data.stepCount));

    // Battery bar — kept clear of the nav bar reserved at the bottom
    // (TFT_HEIGHT - NAV_BAR_HEIGHT), unlike before touch navigation existed.
    const int barX = 4, barY = 118, barW = kScreenW - 8, barH = 10;
    tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
    int fillW = static_cast<int>((barW - 2) * (data.batteryPercent / 100.0f));
    fillW = std::max(0, std::min(fillW, barW - 2));
    uint16_t fillColor = data.batteryLow ? TFT_RED : TFT_GREEN;
    tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, fillColor);

    tft.setCursor(4, 132);
    tft.printf("Batt: %.0f%%", data.batteryPercent);

    drawNavBar();
}

float UiScreens::detailValue(const SensorSnapshot &data) const {
    switch (_detailMetric) {
        case DetailMetric::HeartRate:
            return data.heartRateBpm;
        case DetailMetric::Spo2:
            return data.spo2Percent;
        case DetailMetric::Temperature:
            return data.skinTempC;
        case DetailMetric::Steps:
            return static_cast<float>(data.stepCount);
        case DetailMetric::Battery:
            return data.batteryPercent;
    }
    return 0.0f;
}

const char *UiScreens::detailLabel() const {
    switch (_detailMetric) {
        case DetailMetric::HeartRate:
            return "Heart Rate (bpm)";
        case DetailMetric::Spo2:
            return "SpO2 (%)";
        case DetailMetric::Temperature:
            return "Skin Temp (C)";
        case DetailMetric::Steps:
            return "Steps";
        case DetailMetric::Battery:
            return "Battery (%)";
    }
    return "";
}

void UiScreens::pushTrendSample(float value) {
    _trend[_trendIndex] = value;
    _trendIndex = (_trendIndex + 1) % kTrendPoints;
    if (_trendCount < kTrendPoints) {
        _trendCount++;
    }
}

void UiScreens::drawTrend(int x, int y, int w, int h) {
    TFT_eSPI &tft = _screen->raw();
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    if (_trendCount < 2) {
        return;
    }

    float minV = _trend[0];
    float maxV = _trend[0];
    for (int i = 0; i < _trendCount; i++) {
        minV = std::min(minV, _trend[i]);
        maxV = std::max(maxV, _trend[i]);
    }
    if (maxV - minV < 1.0f) {
        maxV = minV + 1.0f;  // avoid a divide-by-zero on a flat trace
    }

    int oldest = (_trendIndex - _trendCount + kTrendPoints) % kTrendPoints;
    int prevX = -1;
    int prevY = -1;
    for (int i = 0; i < _trendCount; i++) {
        int idx = (oldest + i) % kTrendPoints;
        int px = x + 2 + (w - 4) * i / (kTrendPoints - 1);
        int py = y + h - 2 - static_cast<int>((h - 4) * (_trend[idx] - minV) / (maxV - minV));
        if (prevX >= 0) {
            tft.drawLine(prevX, prevY, px, py, TFT_CYAN);
        }
        prevX = px;
        prevY = py;
    }
}

void UiScreens::renderDetail(const SensorSnapshot &data) {
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setTextSize(1);
    tft.setCursor(4, 4);
    tft.print(detailLabel());

    tft.setTextSize(4);
    tft.setCursor(4, 20);
    tft.print(detailValue(data), 0);

    pushTrendSample(detailValue(data));
    drawTrend(4, 70, kScreenW - 8, 60);

    drawNavBar();
}

void UiScreens::renderAlert(const SensorSnapshot &data) {
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);

    tft.setTextSize(2);
    tft.setCursor(8, 40);
    tft.print("ALERT");

    tft.setTextSize(1);
    tft.setCursor(8, 70);
    // Mirrors sensorDataIsAlert()'s conditions so the listed reason always
    // matches why this screen is showing; a signal-invalid reading of 0
    // must not itself look like an out-of-range alert.
    if (data.ppgSignalValid && (data.heartRateBpm < ALERT_HR_LOW_BPM || data.heartRateBpm > ALERT_HR_HIGH_BPM)) {
        tft.println("HR out of range");
    }
    if (data.ppgSignalValid && data.spo2Percent < ALERT_SPO2_LOW_PCT) {
        tft.println("Low SpO2");
    }
    if (data.skinTempC < ALERT_TEMP_LOW_C || data.skinTempC > ALERT_TEMP_HIGH_C) {
        tft.println("Temp out of range");
    }
    if (data.fallDetected) {
        tft.println("Fall detected");
    }
    if (data.batteryLow) {
        tft.println("Battery low");
    }
}

void UiScreens::renderSettings() {
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    tft.setCursor(4, 4);
    tft.println("Settings");

    tft.setCursor(4, kSettingsBrightnessRowY);
    tft.printf("Brightness: %s (tap)", BRIGHTNESS_LABELS[_brightnessLevel]);

    tft.setCursor(4, kSettingsSyncRowY);
    tft.println("Sync now (tap)");

    tft.setCursor(4, kSettingsAlertRowY);
    tft.println("Alert limits: phone app only");

    drawNavBar();
}

SettingsZone UiScreens::settingsZoneAt(int16_t y) {
    if (y >= kSettingsBrightnessRowY && y < kSettingsBrightnessRowY + kSettingsRowTapHeight) {
        return SettingsZone::Brightness;
    }
    if (y >= kSettingsSyncRowY && y < kSettingsSyncRowY + kSettingsRowTapHeight) {
        return SettingsZone::SyncNow;
    }
    return SettingsZone::None;
}

void UiScreens::drawNavBar() {
    TFT_eSPI &tft = _screen->raw();
    const int barY = TFT_HEIGHT - NAV_BAR_HEIGHT;
    tft.fillRect(0, barY, kScreenW, NAV_BAR_HEIGHT, TFT_NAVY);
    tft.drawFastHLine(0, barY, kScreenW, TFT_WHITE);

    // zoneW must match navZoneAt()'s below exactly, or taps land on a
    // different screen than the label under the finger.
    static const char *kLabels[3] = {"Home", "Detail", "Set"};
    static const UiScreen kZoneScreens[3] = {UiScreen::Home, UiScreen::Detail, UiScreen::Settings};
    const int zoneW = kScreenW / 3;

    tft.setTextSize(1);
    for (int i = 0; i < 3; i++) {
        tft.setTextColor(kZoneScreens[i] == _current ? TFT_YELLOW : TFT_WHITE, TFT_NAVY);
        tft.setCursor(i * zoneW + 6, barY + 5);
        tft.print(kLabels[i]);
    }
}

UiScreen UiScreens::navZoneAt(int16_t x) {
    const int zoneW = kScreenW / 3;
    if (x < zoneW) {
        return UiScreen::Home;
    }
    if (x < zoneW * 2) {
        return UiScreen::Detail;
    }
    return UiScreen::Settings;
}

void UiScreens::renderCalibrationPrompt(int pointNumber, int16_t x, int16_t y) {
    if (_screen == nullptr) {
        return;
    }
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);

    tft.setCursor(4, 4);
    tft.printf("Touch calibration %d/2", pointNumber);
    tft.setCursor(4, 18);
    tft.println("Tap the crosshair.");
    tft.setCursor(4, 30);
    tft.println("Press the button to skip.");

    tft.drawFastHLine(x - 6, y, 13, TFT_YELLOW);
    tft.drawFastVLine(x, y - 6, 13, TFT_YELLOW);
}

void UiScreens::renderBootError(const String &failedList) {
    if (_screen == nullptr) {
        return;
    }
    TFT_eSPI &tft = _screen->raw();
    tft.fillScreen(TFT_ORANGE);
    tft.setTextColor(TFT_BLACK, TFT_ORANGE);

    tft.setTextSize(2);
    tft.setCursor(8, 20);
    tft.print("INIT FAILED");

    tft.setTextSize(1);
    tft.setCursor(8, 50);
    tft.println(failedList);

    tft.setCursor(8, 130);
    tft.println("Continuing anyway -");
    tft.setCursor(8, 142);
    tft.println("readings may be wrong.");
}
