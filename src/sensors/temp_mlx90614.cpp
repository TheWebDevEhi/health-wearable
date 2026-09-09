#include "temp_mlx90614.h"

#include "../config.h"

bool TempMlx90614::begin(TwoWire &bus) {
    return _mlx.begin(I2C_ADDR_MLX90614, &bus);
}

void TempMlx90614::update() {
    float objectTempC = _mlx.readObjectTempC() + TEMP_SKIN_TO_BODY_OFFSET_C;

    _smoothBuffer[_smoothIndex] = objectTempC;
    _smoothIndex = (_smoothIndex + 1) % kSmoothingSamples;
    if (_smoothCount < kSmoothingSamples) {
        _smoothCount++;
    }

    float sum = 0.0f;
    for (int i = 0; i < _smoothCount; i++) {
        sum += _smoothBuffer[i];
    }
    _skinTempC = sum / static_cast<float>(_smoothCount);
}
