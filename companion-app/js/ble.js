// Connection layer: owns the Web Bluetooth device/GATT lifecycle and
// publishes decoded values as events. Deliberately knows nothing about the
// DOM — app.js is the only place that touches document.*.

import {
    ALL_SERVICES,
    BATTERY_LEVEL_CHAR,
    BATTERY_SERVICE,
    FLAGS_CHAR,
    HEALTH_THERMOMETER_SERVICE,
    HEART_RATE_MEASUREMENT_CHAR,
    HEART_RATE_SERVICE,
    HISTORY_CHAR,
    MOTION_SERVICE,
    SETTINGS_CHAR,
    SPO2_CHAR,
    STEPS_CHAR,
    TEMPERATURE_MEASUREMENT_CHAR,
} from "./uuids.js";
import {
    decodeBatteryPercent,
    decodeFlags,
    decodeHeartRate,
    decodeHistoryEntry,
    decodeSpo2,
    decodeSteps,
    decodeTemperature,
} from "./decoders.js";
import { encodeDeviceName, encodeRequestHistory, encodeStartOta, encodeWifiCredentials } from "./settings-protocol.js";

// readme.md #6: every connection needs a fresh user tap (no silent
// reconnect), and the link drops when the tab closes. This class does not
// attempt to auto-reconnect — that's a deliberate match to the spec, not an
// oversight.
export class BleClient extends EventTarget {
    constructor() {
        super();
        this.device = null;
        this.settingsCharacteristic = null;
    }

    get connected() {
        return this.device?.gatt?.connected ?? false;
    }

    // Must be called from a user gesture handler (e.g. a button click) —
    // requestDevice() throws otherwise.
    async connect() {
        this.device = await navigator.bluetooth.requestDevice({
            filters: [{ services: [MOTION_SERVICE] }],
            optionalServices: ALL_SERVICES,
        });
        this.device.addEventListener("gattserverdisconnected", () => {
            this._emit("disconnected");
        });

        const server = await this.device.gatt.connect();

        await this._wireCharacteristic(server, HEART_RATE_SERVICE, HEART_RATE_MEASUREMENT_CHAR, (view) =>
            this._emit("heartRate", decodeHeartRate(view)),
        );
        await this._wireCharacteristic(server, MOTION_SERVICE, SPO2_CHAR, (view) =>
            this._emit("spo2", decodeSpo2(view)),
        );
        await this._wireCharacteristic(server, HEALTH_THERMOMETER_SERVICE, TEMPERATURE_MEASUREMENT_CHAR, (view) =>
            this._emit("temperature", decodeTemperature(view)),
        );
        await this._wireCharacteristic(server, BATTERY_SERVICE, BATTERY_LEVEL_CHAR, (view) =>
            this._emit("battery", decodeBatteryPercent(view)),
        );
        await this._wireCharacteristic(server, MOTION_SERVICE, STEPS_CHAR, (view) =>
            this._emit("steps", decodeSteps(view)),
        );
        await this._wireCharacteristic(server, MOTION_SERVICE, FLAGS_CHAR, (view) =>
            this._emit("flags", decodeFlags(view)),
        );
        // History arrives as a burst of individual notifications, one per
        // buffered entry, only once requestHistory() below is sent — not
        // automatically on connect. See the class comment in ble_gatt.h on
        // the firmware side: an earlier version had the firmware push
        // history from its own onConnect handler, which fires before GATT
        // subscription finishes here, silently dropping every notification.
        await this._wireCharacteristic(server, MOTION_SERVICE, HISTORY_CHAR, (view) =>
            this._emit("historyEntry", decodeHistoryEntry(view)),
        );

        // Write-only, no notifications to wire up.
        const motionService = await server.getPrimaryService(MOTION_SERVICE);
        this.settingsCharacteristic = await motionService.getCharacteristic(SETTINGS_CHAR);

        // Now that every characteristic (history included) is actually
        // subscribed, it's safe to ask the firmware to send its backlog.
        await this.requestHistory();

        this._emit("connected", { name: this.device.name });
    }

    async requestHistory() {
        await this.settingsCharacteristic.writeValueWithResponse(encodeRequestHistory());
    }

    disconnect() {
        this.device?.gatt?.disconnect();
    }

    // Neither of these requires the admin-password gate itself — that's a
    // companion-app UI concern (see admin-auth.js), not something the
    // transport layer knows or enforces.
    async writeDeviceName(name) {
        await this.settingsCharacteristic.writeValueWithResponse(encodeDeviceName(name));
    }

    async writeWifiCredentials(ssid, password) {
        await this.settingsCharacteristic.writeValueWithResponse(encodeWifiCredentials(ssid, password));
    }

    // Only sets a flag on the firmware — see encodeStartOta()'s comment.
    // This resolving successfully means the write was acknowledged, not
    // that an update actually happened; there's no BLE-side confirmation
    // of the OTA result yet.
    async startOtaUpdate() {
        await this.settingsCharacteristic.writeValueWithResponse(encodeStartOta());
    }

    async _wireCharacteristic(server, serviceUuid, charUuid, onValue) {
        const service = await server.getPrimaryService(serviceUuid);
        const characteristic = await service.getCharacteristic(charUuid);
        characteristic.addEventListener("characteristicvaluechanged", (event) => {
            onValue(event.target.value);
        });
        // Some characteristics (steps, battery, SpO2) are READ | NOTIFY —
        // startNotifications() still works for both properties and gives an
        // immediate baseline read via the first notification.
        await characteristic.startNotifications();
    }

    _emit(type, detail) {
        this.dispatchEvent(new CustomEvent(type, { detail }));
    }
}

export function isWebBluetoothSupported() {
    return typeof navigator !== "undefined" && "bluetooth" in navigator;
}
