#include "motion_lis3dh.h"

#include <cmath>

#include "../config.h"

bool MotionLis3dh::begin(TwoWire &bus) {
    (void)bus;  // see header note on Adafruit_LIS3DH's fixed bus binding
    if (!_lis.begin(I2C_ADDR_LIS3DH)) {
        return false;
    }
    _lis.setRange(LIS3DH_RANGE_2_G);
    return true;
}

bool MotionLis3dh::configureDoubleTapWake() {
    // c=2 selects double-click-only interrupt output. Threshold is a
    // placeholder — tune against a real double-tap vs. ordinary arm
    // movement at bring-up (readme.md #5, #11.5).
    _lis.setClick(2, /*threshold=*/40);
    return true;
}

void MotionLis3dh::update() {
    _lis.read();

    // TODO: replace with a real step-counting algorithm (peak detection on
    // acceleration magnitude) and a fall heuristic (readme.md #3). For now
    // this only reports whether the arm is moving, which is what PPG
    // motion-gating needs.
    float magnitude = std::sqrt(_lis.x_g * _lis.x_g + _lis.y_g * _lis.y_g + _lis.z_g * _lis.z_g);
    _isMoving = std::fabs(magnitude - 1.0f) > kMovementThresholdG;
}
