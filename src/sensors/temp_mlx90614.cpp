#include "temp_mlx90614.h"

#include "../config.h"

bool TempMlx90614::begin(TwoWire &bus) {
    // TODO: initialize the Adafruit MLX90614 driver on I2C_ADDR_MLX90614.
    (void)bus;
    return false;
}

void TempMlx90614::update() {
    // TODO: read object temperature, apply TEMP_SKIN_TO_BODY_OFFSET_C, and
    // smooth across a few samples (readme.md #3).
}
