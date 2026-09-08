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

// Owns which screen is showing and redraws on change; navigated with the two
// side buttons (readme.md #4).
class UiScreens {
public:
    void begin(Screen &screen);
    void show(UiScreen screen);
    void render(const SensorSnapshot &data);

private:
    Screen *_screen = nullptr;
    UiScreen _current = UiScreen::Home;

    void renderHome(const SensorSnapshot &data);
    void renderDetail(const SensorSnapshot &data);
    void renderAlert(const SensorSnapshot &data);
    void renderSettings();
};
