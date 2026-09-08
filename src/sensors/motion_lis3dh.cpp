#include "motion_lis3dh.h"

#include "../config.h"

bool MotionLis3dh::begin(TwoWire &bus) {
    // TODO: initialize the Adafruit LIS3DH driver on I2C_ADDR_LIS3DH, and
    // enable the FIFO and interrupt lines (readme.md #3).
    (void)bus;
    return false;
}

bool MotionLis3dh::configureDoubleTapWake() {
    // TODO: call setClick(2, threshold) and tune threshold/timing so a
    // deliberate double-tap fires but ordinary arm movement does not
    // (readme.md #5, #11.5).
    return false;
}

void MotionLis3dh::update() {
    // TODO: drain the FIFO, update step count / activity state, and set
    // _fallDetected from the fall heuristic.
}
