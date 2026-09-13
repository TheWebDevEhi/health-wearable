#pragma once

#include <Adafruit_LIS3DH.h>
#include <Wire.h>

// LIS3DH: steps, activity state, double-tap wake, fall flag (readme.md #3,
// #5). Also the sensor the firmware leans on to reject motion-corrupted PPG
// windows.
class MotionLis3dh {
public:
    bool begin(TwoWire &bus);

    // Configures the hardware double-tap engine so the sensor can wake the
    // chip on GPIO1 while the MCU is in deep sleep.
    bool configureDoubleTapWake();

    void update();

    uint32_t stepCount() const { return _stepCount; }
    bool isMoving() const { return _isMoving; }
    bool fallDetected() const { return _fallDetected; }

private:
    // Adafruit_LIS3DH binds its I2C bus at construction, not at begin(), so
    // this only supports the global Wire instance — the only bus this
    // hardware has (readme.md #8).
    Adafruit_LIS3DH _lis{&Wire};

    uint32_t _stepCount = 0;
    bool _isMoving = false;
    bool _fallDetected = false;

    // Movement threshold on |acceleration - 1g|, feeding isMoving() (used
    // for PPG motion-gating) — separate from the step/fall heuristics
    // below, which have their own thresholds.
    static constexpr float kMovementThresholdG = 0.15f;

    // Step counting: a simple rising-edge counter on acceleration
    // magnitude, not real gait analysis — counts once per crossing above
    // kStepThresholdG, debounced so one physical step's single rise-then-
    // fall isn't counted twice. Placeholder values; tune against real
    // walking data at bring-up (readme.md #3, #11).
    static constexpr float kStepThresholdG = 1.2f;
    static constexpr uint32_t kStepDebounceMs = 250;  // caps counting at 240 steps/min
    bool _wasAboveStepThreshold = false;
    uint32_t _lastStepMs = 0;

    // Fall detection: the standard simple two-stage heuristic — free-fall
    // (magnitude drops below kFreeFallThresholdG, near-weightless) followed
    // within kFallWindowMs by an impact spike (magnitude above
    // kImpactThresholdG) — not a validated fall-detection algorithm.
    // Placeholder values; tune against real fall/drop data at bring-up
    // (readme.md #3, #11).
    static constexpr float kFreeFallThresholdG = 0.4f;
    static constexpr float kImpactThresholdG = 2.5f;
    static constexpr uint32_t kFallWindowMs = 1000;
    uint32_t _freeFallStartMs = 0;  // 0 = no free-fall candidate armed

    // How long a detected fall stays flagged before auto-clearing. There's
    // no button/touch "acknowledge" wired to this alert specifically
    // (readme.md #5 doesn't call for one), so it has to clear itself
    // rather than latch forever and permanently occupy the Alert screen —
    // unlike the HR/SpO2/temp alerts, which clear naturally once the
    // underlying reading is back in range. Long enough to notice; not a
    // validated value.
    static constexpr uint32_t kFallAlertDurationMs = 10000;
    uint32_t _fallDetectedAtMs = 0;
};
