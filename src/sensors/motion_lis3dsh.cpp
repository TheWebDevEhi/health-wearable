#include "motion_lis3dsh.h"

#include <Arduino.h>  // millis() — don't rely on it arriving transitively via Wire.h
#include <cmath>

#include "../config.h"

namespace {
// LIS3DSH register map — from STMicroelectronics' own stm32-lis3dsh driver
// source (lis3dsh.h), not the datasheet's prose alone. This chip has an
// entirely different register layout from the LIS3DH originally speced for
// this board (see motion_lis3dsh.h's class comment).
constexpr uint8_t kRegWhoAmI   = 0x0F;
constexpr uint8_t kWhoAmIValue = 0x3F;  // LIS3DH's would be 0x33 — different chip
constexpr uint8_t kRegCtrlReg4 = 0x20;
constexpr uint8_t kRegCtrlReg5 = 0x24;
constexpr uint8_t kRegOutXL    = 0x28;  // X/Y/Z: 6 consecutive bytes from here

// CTRL_REG4: ODR[7:4] | BDU[3] | ZEN[2] | YEN[1] | XEN[0]. 100 Hz
// comfortably covers this project's sample rate (sensorTask ticks every
// 100ms); BDU (block data update) avoids tearing a reading across two I2C
// transactions if one lands mid-update.
constexpr uint8_t kOdr100Hz       = 0x60;
constexpr uint8_t kBlockDataUpdate = 0x08;
constexpr uint8_t kAxesXYZEnable   = 0x07;
constexpr uint8_t kCtrlReg4Value  = kOdr100Hz | kBlockDataUpdate | kAxesXYZEnable;

// CTRL_REG5 bits[5:3] full-scale select (000=+/-2g, 001=+/-4g, 010=+/-6g,
// 011=+/-8g, 100=+/-16g). +/-8g mirrors the same headroom decision made for
// the originally-speced LIS3DH (see DEVELOPMENT.md): a real fall's impact
// spike commonly exceeds 2g, and kImpactThresholdG (2.5g, below) needs room
// above the range ceiling.
constexpr uint8_t kCtrlReg5Value = 0x18;  // FSCALE = 011 -> +/-8g

// mg per digit at +/-8g, taken directly from ST's own sensitivity table
// (LIS3DSH_SENSITIVITY_0_24G), not derived. The raw 16-bit register value
// is a 12-bit reading left-justified in a 16-bit word — ST's own driver
// right-shifts by 4 to get the effective digit count before this scale
// applies ("ACC[mg] = SENSITIVITY * (out_h*256+out_l)/16").
constexpr float kSensitivityMgPerDigit = 0.24f;
}  // namespace

bool MotionLis3dsh::begin(TwoWire &bus) {
    _bus = &bus;

    uint8_t whoAmI = 0;
    if (!readReg(kRegWhoAmI, whoAmI) || whoAmI != kWhoAmIValue) {
        return false;
    }
    if (!writeReg(kRegCtrlReg4, kCtrlReg4Value)) {
        return false;
    }
    if (!writeReg(kRegCtrlReg5, kCtrlReg5Value)) {
        return false;
    }
    return true;
}

bool MotionLis3dsh::writeReg(uint8_t reg, uint8_t value) {
    _bus->beginTransmission(I2C_ADDR_LIS3DSH);
    _bus->write(reg);
    _bus->write(value);
    return _bus->endTransmission() == 0;
}

bool MotionLis3dsh::readReg(uint8_t reg, uint8_t &value) {
    _bus->beginTransmission(I2C_ADDR_LIS3DSH);
    _bus->write(reg);
    // Repeated start (no stop bit) so the bus stays held for the read that
    // follows — releasing it here would let another I2C transaction (there
    // are three other sensors on this shared bus) land between the write
    // and the read.
    if (_bus->endTransmission(false) != 0) {
        return false;
    }
    if (_bus->requestFrom(I2C_ADDR_LIS3DSH, static_cast<uint8_t>(1)) != 1) {
        return false;
    }
    value = static_cast<uint8_t>(_bus->read());
    return true;
}

bool MotionLis3dsh::readAxes(int16_t &x, int16_t &y, int16_t &z) {
    _bus->beginTransmission(I2C_ADDR_LIS3DSH);
    _bus->write(kRegOutXL);
    if (_bus->endTransmission(false) != 0) {
        return false;
    }
    if (_bus->requestFrom(I2C_ADDR_LIS3DSH, static_cast<uint8_t>(6)) != 6) {
        return false;
    }
    uint8_t xl = static_cast<uint8_t>(_bus->read());
    uint8_t xh = static_cast<uint8_t>(_bus->read());
    uint8_t yl = static_cast<uint8_t>(_bus->read());
    uint8_t yh = static_cast<uint8_t>(_bus->read());
    uint8_t zl = static_cast<uint8_t>(_bus->read());
    uint8_t zh = static_cast<uint8_t>(_bus->read());
    x = static_cast<int16_t>((xh << 8) | xl);
    y = static_cast<int16_t>((yh << 8) | yl);
    z = static_cast<int16_t>((zh << 8) | zl);
    return true;
}

void MotionLis3dsh::update() {
    int16_t rawX, rawY, rawZ;
    if (!readAxes(rawX, rawY, rawZ)) {
        return;  // transient I2C error on this tick — keep last-known state, retry next tick
    }
    uint32_t nowMs = millis();

    // >>4 sign-extends on this toolchain (arithmetic shift on a signed
    // type) — same assumption already relied on elsewhere in this codebase
    // (touch_xpt2046.cpp's calibration math), not a new one introduced here.
    float xG = (static_cast<float>(rawX >> 4) * kSensitivityMgPerDigit) / 1000.0f;
    float yG = (static_cast<float>(rawY >> 4) * kSensitivityMgPerDigit) / 1000.0f;
    float zG = (static_cast<float>(rawZ >> 4) * kSensitivityMgPerDigit) / 1000.0f;

    float magnitude = std::sqrt(xG * xG + yG * yG + zG * zG);
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
