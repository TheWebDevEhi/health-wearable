// GATT service/characteristic UUIDs — must exactly match src/comms/ble_gatt.cpp
// on the firmware side. Standard BLE SIG UUIDs are plain 16-bit numbers;
// Web Bluetooth resolves those to their full 128-bit form itself. Custom
// UUIDs are project-defined (see DEVELOPMENT.md there) and must not drift
// from what the firmware actually advertises.

export const HEART_RATE_SERVICE = 0x180d;
export const HEART_RATE_MEASUREMENT_CHAR = 0x2a37;

export const HEALTH_THERMOMETER_SERVICE = 0x1809;
export const TEMPERATURE_MEASUREMENT_CHAR = 0x2a1c;

export const BATTERY_SERVICE = 0x180f;
export const BATTERY_LEVEL_CHAR = 0x2a19;

export const MOTION_SERVICE = "157a8ee9-7a76-4d8f-9fcf-b7d815451acc";
export const SPO2_CHAR = "a8fb0a1c-eb12-47a6-8ffb-d65d1dc4eaeb";
export const STEPS_CHAR = "71682870-7fc9-4624-811d-b3de04a1731f";
export const FLAGS_CHAR = "0f0218c8-c147-4081-a7af-a9e539a3b5a6";
export const HISTORY_CHAR = "a565c5f9-ce1e-42b3-95f6-b54504ad1286";
export const SETTINGS_CHAR = "ddb216da-d1f7-4301-994b-112316a5d4fd";

// Every service the app touches, for requestDevice()'s optionalServices —
// Web Bluetooth throws a SecurityError on getPrimaryService() for anything
// not listed here (or used as a scan filter).
export const ALL_SERVICES = [HEART_RATE_SERVICE, HEALTH_THERMOMETER_SERVICE, BATTERY_SERVICE, MOTION_SERVICE];
