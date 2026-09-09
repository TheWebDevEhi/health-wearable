// Pure decoders for each characteristic's raw bytes, matching the exact
// wire formats src/comms/ble_gatt.cpp writes. No DOM/BLE dependencies here —
// keep these testable against synthetic DataViews (see DEVELOPMENT.md).

// Heart Rate Measurement (0x2A37, BLE SIG spec): flags byte, then either a
// uint8 or uint16 bpm depending on flag bit 0. The firmware only ever sends
// the uint8 form, but a correct client decodes both.
export function decodeHeartRate(view) {
    const flags = view.getUint8(0);
    const is16Bit = (flags & 0x01) !== 0;
    return is16Bit ? view.getUint16(1, true) : view.getUint8(1);
}

// SpO2 (custom, a8fb0a1c-...): plain uint8 percent — no flags byte, unlike
// the standard characteristics above.
export function decodeSpo2(view) {
    return view.getUint8(0);
}

// Temperature Measurement (0x2A1C, BLE SIG spec): flags byte, then an
// IEEE-11073 32-bit FLOAT. This is NOT a plain float32 — top byte is an
// 8-bit two's-complement exponent, low 3 bytes are a 24-bit two's-complement
// mantissa; value = mantissa * 10^exponent. Inverse of
// encodeIeee11073Float() in ble_gatt.cpp.
export function decodeTemperature(view) {
    const raw = view.getInt32(1, true);
    const exponent = raw >> 24; // arithmetic shift sign-extends the top byte
    const mantissa = (raw << 8) >> 8; // isolate + sign-extend the low 24 bits
    return mantissa * Math.pow(10, exponent);
}

// Battery Level (0x2A19, BLE SIG spec): plain uint8 percent.
export function decodeBatteryPercent(view) {
    return view.getUint8(0);
}

// Steps (custom, 71682870-...): uint32, little-endian.
export function decodeSteps(view) {
    return view.getUint32(0, true);
}

// Flags (custom, 0f0218c8-...): uint8 bitfield — bit0 moving, bit1 fall,
// bit2 alert, bit3 battery low. Must mirror the bit assignment in
// BleGatt::notify() exactly.
export function decodeFlags(view) {
    const byte = view.getUint8(0);
    return {
        moving: (byte & 0x01) !== 0,
        fallDetected: (byte & 0x02) !== 0,
        alert: (byte & 0x04) !== 0,
        batteryLow: (byte & 0x08) !== 0,
    };
}

// History entry (custom, a565c5f9-..., one notification per buffered
// sample): packed struct { uint32 ageMs; int16 heartRateBpmX10;
// int16 spo2PercentX10; int16 skinTempCx10 } — 10 bytes, matching
// BleGatt::HistoryEntry exactly (fixed-point x10 fields).
export function decodeHistoryEntry(view) {
    return {
        ageMs: view.getUint32(0, true),
        heartRateBpm: view.getInt16(4, true) / 10,
        spo2Percent: view.getInt16(6, true) / 10,
        skinTempC: view.getInt16(8, true) / 10,
    };
}
