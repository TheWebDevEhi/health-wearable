#include "ui_screens.h"

void UiScreens::begin(Screen &screen) {
    _screen = &screen;
}

void UiScreens::show(UiScreen screen) {
    _current = screen;
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
    // TODO: large HR/SpO2 numbers, temp, steps, and a battery bar underneath
    // (readme.md #4).
    (void)data;
}

void UiScreens::renderDetail(const SensorSnapshot &data) {
    // TODO: one signal at a time with a short scrolling trend line.
    (void)data;
}

void UiScreens::renderAlert(const SensorSnapshot &data) {
    // TODO: full-screen warning, mirrored to the status LED and to the phone.
    (void)data;
}

void UiScreens::renderSettings() {
    // TODO: brightness, alert limits, and a "sync now" action.
}
