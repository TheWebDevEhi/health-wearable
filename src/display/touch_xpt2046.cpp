#include "touch_xpt2046.h"

bool TouchXpt2046::begin() {
    return _touch.begin();
}

bool TouchXpt2046::pressed() {
    return _touch.touched();
}

bool TouchXpt2046::readRaw(uint16_t &x, uint16_t &y) {
    if (!_touch.touched()) {
        return false;
    }
    TS_Point p = _touch.getPoint();
    // TOUCH_SWAP_XY (config.h): whether the panel's raw axes are rotated
    // relative to the display's drawn coordinates is a physical-mounting
    // fact, unverifiable without a real unit — see the flag's own comment.
    x = TOUCH_SWAP_XY ? p.y : p.x;
    y = TOUCH_SWAP_XY ? p.x : p.y;
    return true;
}

void TouchXpt2046::setCalibration(uint16_t rawX0, uint16_t rawY0, int16_t screenX0, int16_t screenY0,
                                   uint16_t rawX1, uint16_t rawY1, int16_t screenX1, int16_t screenY1) {
    _rawX0 = rawX0;
    _rawY0 = rawY0;
    _screenX0 = screenX0;
    _screenY0 = screenY0;
    _rawX1 = rawX1;
    _rawY1 = rawY1;
    _screenX1 = screenX1;
    _screenY1 = screenY1;
    _calibrated = true;
}

bool TouchXpt2046::readCalibrated(int16_t &x, int16_t &y) {
    if (!_calibrated) {
        return false;
    }
    uint16_t rawX, rawY;
    if (!readRaw(rawX, rawY)) {
        return false;
    }

    // Linear interpolation from the two calibration points, per axis.
    // Guarded against a zero raw span (both calibration touches reading
    // the same raw value) rather than dividing by it — shouldn't happen
    // with a working panel, but a stuck or unwired touch line during
    // bring-up could otherwise turn into a divide-by-zero.
    int32_t rawDx = static_cast<int32_t>(_rawX1) - static_cast<int32_t>(_rawX0);
    int32_t rawDy = static_cast<int32_t>(_rawY1) - static_cast<int32_t>(_rawY0);
    int32_t screenDx = static_cast<int32_t>(_screenX1) - static_cast<int32_t>(_screenX0);
    int32_t screenDy = static_cast<int32_t>(_screenY1) - static_cast<int32_t>(_screenY0);

    x = (rawDx == 0) ? _screenX0
                      : static_cast<int16_t>(_screenX0 + (static_cast<int32_t>(rawX) - _rawX0) * screenDx / rawDx);
    y = (rawDy == 0) ? _screenY0
                      : static_cast<int16_t>(_screenY0 + (static_cast<int32_t>(rawY) - _rawY0) * screenDy / rawDy);
    return true;
}
