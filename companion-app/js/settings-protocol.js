// Encoders for the settings characteristic's write commands. Must exactly
// match the parser in src/comms/ble_gatt.cpp::SettingsCallbacks::onWrite —
// see DEVELOPMENT.md "Settings wire protocol" on the firmware side. Pure
// functions, no BLE dependency, same shape as decoders.js.

export const CMD_SET_DEVICE_NAME = 0x01;
export const CMD_SET_WIFI_CREDENTIALS = 0x02;
export const CMD_REQUEST_HISTORY = 0x03;
export const CMD_START_OTA = 0x04;

// Must match kMaxDeviceNameLen/kMaxSsidLen/kMaxPasswordLen in ble_gatt.cpp.
export const MAX_DEVICE_NAME_BYTES = 20;
export const MAX_SSID_BYTES = 32;
export const MAX_PASSWORD_BYTES = 64;

// [0x01][name bytes]
export function encodeDeviceName(name) {
    const bytes = new TextEncoder().encode(name);
    if (bytes.length === 0 || bytes.length > MAX_DEVICE_NAME_BYTES) {
        throw new Error(`Device name must be 1-${MAX_DEVICE_NAME_BYTES} bytes (UTF-8).`);
    }
    const payload = new Uint8Array(1 + bytes.length);
    payload[0] = CMD_SET_DEVICE_NAME;
    payload.set(bytes, 1);
    return payload;
}

// [0x02][ssidLen][ssid bytes][passLen][password bytes]
export function encodeWifiCredentials(ssid, password) {
    const ssidBytes = new TextEncoder().encode(ssid);
    const passBytes = new TextEncoder().encode(password);
    if (ssidBytes.length === 0 || ssidBytes.length > MAX_SSID_BYTES) {
        throw new Error(`SSID must be 1-${MAX_SSID_BYTES} bytes (UTF-8).`);
    }
    if (passBytes.length > MAX_PASSWORD_BYTES) {
        throw new Error(`Password must be at most ${MAX_PASSWORD_BYTES} bytes (UTF-8).`);
    }
    const payload = new Uint8Array(1 + 1 + ssidBytes.length + 1 + passBytes.length);
    let pos = 0;
    payload[pos++] = CMD_SET_WIFI_CREDENTIALS;
    payload[pos++] = ssidBytes.length;
    payload.set(ssidBytes, pos);
    pos += ssidBytes.length;
    payload[pos++] = passBytes.length;
    payload.set(passBytes, pos);
    return payload;
}

// [0x03], no payload. Must be sent only after the client has finished
// subscribing to the history characteristic — see ble.js's connect() and
// the class comment in ble_gatt.h on the firmware side for why the
// firmware no longer pushes history automatically on connect.
export function encodeRequestHistory() {
    return new Uint8Array([CMD_REQUEST_HISTORY]);
}

// [0x04], no payload. Firmware only sets a flag on receipt (BleGatt is not
// allowed to block on Wi-Fi from inside a NimBLE callback) — a separate
// firmware task picks it up and actually runs the update, so there's no
// immediate confirmation this write causes; readme.md #7 still has no real
// OTA server to test the actual transfer against.
export function encodeStartOta() {
    return new Uint8Array([CMD_START_OTA]);
}
