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
    x = p.x;
    y = p.y;
    // TODO: apply the calibration mapping to panel coordinates (readme.md #11).
    return true;
}
