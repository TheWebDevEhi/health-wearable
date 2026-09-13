#include "motion_lis3dh.h"

#include <Arduino.h>  // millis() — don't rely on it arriving transitively via Wire.h/Adafruit_LIS3DH.h
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
    uint32_t nowMs = millis();

    float magnitude = std::sqrt(_lis.x_g * _lis.x_g + _lis.y_g * _lis.y_g + _lis.z_g * _lis.z_g);
    _isMoving = std::fabs(magnitude - 1.0f) > kMovementThresholdG;

    // Step counting: count on each rising edge above kStepThresholdG,
    // debounced by kStepDebounceMs so a single step's rise-then-fall in
    // magnitude isn't counted twice.
    bool aboveStepThreshold = magnitude > kStepThresholdG;
    if (aboveStepThreshold && !_wasAboveStepThreshold && (nowMs - _lastStepMs) >= kStepDebounceMs) {
        _stepCount++;
        _lastStepMs = nowMs;
    }
    _wasAboveStepThreshold = aboveStepThreshold;

    // Fall detection: arm a candidate on free-fall, confirm it on an
    // impact spike within the window. Re-arming on every low reading
    // (rather than only the first) anchors the window to "impact must
    // follow the most recent free-fall sample soon," which tracks real
    // fall physics better than anchoring to when free-fall first began.
    if (magnitude < kFreeFallThresholdG) {
        _freeFallStartMs = nowMs;
    }
    if (_freeFallStartMs != 0 && (nowMs - _freeFallStartMs) <= kFallWindowMs &&
        magnitude > kImpactThresholdG) {
        _fallDetected = true;
        _fallDetectedAtMs = nowMs;
        _freeFallStartMs = 0;  // consumed — don't let the same event re-trigger
    }
    if (_fallDetected && (nowMs - _fallDetectedAtMs) > kFallAlertDurationMs) {
        _fallDetected = false;
    }
}
