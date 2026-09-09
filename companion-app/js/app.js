// DOM wiring only — all decoding lives in decoders.js, all BLE lifecycle
// lives in ble.js, all settings-command encoding lives in
// settings-protocol.js. Keep it that way so those can be tested/reused
// without a document object.

import { BleClient, isWebBluetoothSupported } from "./ble.js";
import { currentTheme, getStoredTheme, setTheme } from "./theme.js";
import { hasAdminPassword, isUnlockedThisSession, markUnlocked, setAdminPassword, verifyAdminPassword } from "./admin-auth.js";
import { promptPassword } from "./password-dialog.js";

const el = {
    statusBadge: document.getElementById("status-badge"),
    unsupported: document.getElementById("unsupported"),
    connectScreen: document.getElementById("connect-screen"),
    connectButton: document.getElementById("connect-button"),
    dashboard: document.getElementById("dashboard"),
    disconnectButton: document.getElementById("disconnect-button"),
    alertBanner: document.getElementById("alert-banner"),
    hr: document.getElementById("hr-value"),
    spo2: document.getElementById("spo2-value"),
    temp: document.getElementById("temp-value"),
    steps: document.getElementById("steps-value"),
    battery: document.getElementById("battery-value"),
    batteryFill: document.getElementById("battery-fill"),
    historyCanvas: document.getElementById("history-canvas"),
    historyEmpty: document.getElementById("history-empty"),
    themeSelect: document.getElementById("theme-select"),
    deviceNameInput: document.getElementById("device-name-input"),
    deviceNameSave: document.getElementById("device-name-save"),
    wifiSsidInput: document.getElementById("wifi-ssid-input"),
    wifiPasswordInput: document.getElementById("wifi-password-input"),
    wifiSave: document.getElementById("wifi-save"),
    otaStart: document.getElementById("ota-start"),
    settingsStatus: document.getElementById("settings-status"),
};

const history = []; // { ageMs, heartRateBpm, spo2Percent, skinTempC }, oldest-first as received

function setConnected(connected) {
    el.statusBadge.textContent = connected ? "Connected" : "Disconnected";
    el.statusBadge.classList.toggle("badge-connected", connected);
    el.statusBadge.classList.toggle("badge-disconnected", !connected);
    el.connectScreen.hidden = connected;
    el.dashboard.hidden = !connected;
}

function showSettingsStatus(message, isError) {
    el.settingsStatus.textContent = message;
    el.settingsStatus.style.color = isError ? "var(--danger)" : "var(--ok)";
}

function drawHistory() {
    const canvas = el.historyCanvas;
    const ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (history.length < 2) {
        el.historyEmpty.hidden = history.length !== 0;
        return;
    }
    el.historyEmpty.hidden = true;

    const values = history.map((h) => h.heartRateBpm);
    const minV = Math.min(...values);
    const maxV = Math.max(...values) || minV + 1;
    const span = Math.max(maxV - minV, 1);

    ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#38bdf8";
    ctx.lineWidth = 2;
    ctx.beginPath();
    history.forEach((entry, i) => {
        const x = (i / (history.length - 1)) * canvas.width;
        const y = canvas.height - ((entry.heartRateBpm - minV) / span) * (canvas.height - 16) - 8;
        if (i === 0) {
            ctx.moveTo(x, y);
        } else {
            ctx.lineTo(x, y);
        }
    });
    ctx.stroke();
}

// Prompts to set an admin password on first use, or to enter the existing
// one otherwise; unlocks for the rest of the tab session once correct. See
// admin-auth.js for what this is (and isn't) protecting against.
async function requireAdminUnlock() {
    if (isUnlockedThisSession()) {
        return true;
    }

    if (!hasAdminPassword()) {
        const first = await promptPassword("Set an admin password to protect Wi-Fi settings:");
        if (!first) {
            return false;
        }
        const confirm = await promptPassword("Confirm the password:");
        if (first !== confirm) {
            showSettingsStatus("Passwords didn't match — try again.", true);
            return false;
        }
        await setAdminPassword(first);
        markUnlocked();
        return true;
    }

    const entered = await promptPassword("Enter admin password:");
    if (entered === null) {
        return false;
    }
    if (!(await verifyAdminPassword(entered))) {
        showSettingsStatus("Incorrect admin password.", true);
        return false;
    }
    markUnlocked();
    return true;
}

function wireThemeControl() {
    el.themeSelect.value = getStoredTheme() || currentTheme();
    el.themeSelect.addEventListener("change", () => {
        setTheme(el.themeSelect.value);
        drawHistory(); // re-stroke with the new theme's --accent colour
    });
}

function wireSettingsControls(ble) {
    el.deviceNameSave.addEventListener("click", async () => {
        const name = el.deviceNameInput.value.trim();
        if (!name) {
            showSettingsStatus("Enter a device name first.", true);
            return;
        }
        try {
            await ble.writeDeviceName(name);
            showSettingsStatus(`Device renamed to "${name}". Reconnect to see it in the device list.`, false);
        } catch (err) {
            showSettingsStatus(`Couldn't rename device: ${err.message}`, true);
        }
    });

    el.wifiSave.addEventListener("click", async () => {
        const ssid = el.wifiSsidInput.value.trim();
        const password = el.wifiPasswordInput.value;
        if (!ssid) {
            showSettingsStatus("Enter a network name (SSID) first.", true);
            return;
        }

        const unlocked = await requireAdminUnlock();
        if (!unlocked) {
            return;
        }

        try {
            await ble.writeWifiCredentials(ssid, password);
            showSettingsStatus("Wi-Fi credentials sent to the device.", false);
            el.wifiPasswordInput.value = "";
        } catch (err) {
            showSettingsStatus(`Couldn't send Wi-Fi credentials: ${err.message}`, true);
        }
    });

    el.otaStart.addEventListener("click", async () => {
        const unlocked = await requireAdminUnlock();
        if (!unlocked) {
            return;
        }
        try {
            await ble.startOtaUpdate();
            showSettingsStatus(
                "Update requested. The device will attempt it in the background — no progress is reported here yet.",
                false,
            );
        } catch (err) {
            showSettingsStatus(`Couldn't start update: ${err.message}`, true);
        }
    });
}

function main() {
    wireThemeControl();

    if (!isWebBluetoothSupported()) {
        el.unsupported.hidden = false;
        el.connectScreen.hidden = true;
        return;
    }

    const ble = new BleClient();
    wireSettingsControls(ble);

    el.connectButton.addEventListener("click", async () => {
        el.connectButton.disabled = true;
        try {
            await ble.connect();
        } catch (err) {
            // requestDevice() rejects on user cancel too — not necessarily an error.
            console.warn("[BLE] connect failed or cancelled:", err);
        } finally {
            el.connectButton.disabled = false;
        }
    });

    el.disconnectButton.addEventListener("click", () => ble.disconnect());

    ble.addEventListener("connected", () => {
        // A fresh history backfill is about to arrive (ble.js requests it
        // as the last step of connect()); without this, entries from a
        // previous session kept accumulating on every reconnect — an
        // unbounded, ever-growing array whose sparkline mixed readings
        // from different device boot epochs together.
        history.length = 0;
        drawHistory();
        setConnected(true);
    });
    ble.addEventListener("disconnected", () => {
        setConnected(false);
        // readme.md #6: no silent reconnect — history/tile state is left as
        // last-known-good rather than cleared, so a stale-but-real reading
        // beats a blank dashboard, but the badge makes staleness obvious.
    });

    ble.addEventListener("heartRate", (e) => {
        el.hr.textContent = e.detail;
    });
    ble.addEventListener("spo2", (e) => {
        el.spo2.textContent = e.detail;
    });
    ble.addEventListener("temperature", (e) => {
        el.temp.textContent = e.detail.toFixed(1);
    });
    ble.addEventListener("battery", (e) => {
        el.battery.textContent = e.detail;
        el.batteryFill.style.width = `${Math.max(0, Math.min(100, e.detail))}%`;
    });
    ble.addEventListener("steps", (e) => {
        el.steps.textContent = e.detail;
    });
    ble.addEventListener("flags", (e) => {
        el.alertBanner.hidden = !e.detail.alert;
    });
    ble.addEventListener("historyEntry", (e) => {
        history.push(e.detail);
        drawHistory();
    });
}

main();

if ("serviceWorker" in navigator) {
    window.addEventListener("load", () => {
        navigator.serviceWorker.register("service-worker.js").catch((err) => {
            console.warn("[PWA] service worker registration failed:", err);
        });
    });
}
