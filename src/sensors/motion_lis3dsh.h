#pragma once

#include <Wire.h>

// LIS3DSH: steps, activity state, fall flag (readme.md #3, #5). Also the
// sensor the firmware leans on to reject motion-corrupted PPG windows.
//
// This talks to the chip directly over I2C rather than through a library.
// The board was originally speced around the LIS3DH (Adafruit_LIS3DH), but
// the physical unit on real hardware is a different chip, the LIS3DSH —
// confirmed by a boot-time I2C scan (readme.md #11) that found it
// responding at 0x1D, and by its WHO_AM_I register reading 0x3F, not the
// LIS3DH's 0x33. Different chip, different register map — a driver written
// for LIS3DH cannot talk to this part regardless of address. The register
// addresses and mg/digit sensitivity values here are taken from
// STMicroelectronics' own stm32-lis3dsh reference driver source
// (lis3dsh.h/.c), not the datasheet's prose alone or guessed.
//
// Double-tap wake is NOT implemented here — deliberately deferred (the
// LIS3DSH does support click/double-click detection in silicon via
// CLICK_CFG, just not wired up yet). See power/power_mgr.cpp's wake-source
// comment for the current consequence: the EXT1 wake source tied to this
// chip's interrupt pin is registered but inert until that lands.
class MotionLis3dsh {
public:
    bool begin(TwoWire &bus);
    void update();

    uint32_t stepCount() const { return _stepCount; }
    bool isMoving() const { return _isMoving; }
    bool fallDetected() const { return _fallDetected; }

private:
    TwoWire *_bus = nullptr;

    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t &value);
    bool readAxes(int16_t &x, int16_t &y, int16_t &z);

    uint32_t _stepCount = 0;
    bool _isMoving = false;
    bool _fallDetected = false;

    // Movement threshold on |acceleration - 1g|, feeding isMoving() (used
    // for PPG motion-gating) — separate from the step/fall heuristics
    // below, which have their own thresholds. Unchanged from the original
    // LIS3DH-based tuning — these are "g" thresholds, not chip-specific.
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
