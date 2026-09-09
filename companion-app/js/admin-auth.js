// A client-side "admin password" gate for settings that can affect the
// device's network access (Wi-Fi/OTA credentials) — per the user's own
// request, "a simple frontend side admin password."
//
// This is a deterrent, not real security: the hash lives in this browser's
// localStorage, checked entirely in JS. Anyone with devtools access (or who
// just reads this file) can bypass it outright — there is no server, and
// the firmware itself does not require or check any credential on the BLE
// write (see DEVELOPMENT.md "Settings wire protocol"). It stops an
// accidental tap on the wrong button; it does not stop a determined
// attacker with physical access to the phone.

const STORAGE_KEY = "keis-admin-password-hash";
let sessionUnlocked = false;

async function sha256Hex(text) {
    const bytes = new TextEncoder().encode(text);
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    return Array.from(new Uint8Array(digest))
        .map((b) => b.toString(16).padStart(2, "0"))
        .join("");
}

export function hasAdminPassword() {
    return localStorage.getItem(STORAGE_KEY) !== null;
}

export async function setAdminPassword(password) {
    localStorage.setItem(STORAGE_KEY, await sha256Hex(password));
}

export async function verifyAdminPassword(password) {
    const stored = localStorage.getItem(STORAGE_KEY);
    return stored !== null && (await sha256Hex(password)) === stored;
}

export function isUnlockedThisSession() {
    return sessionUnlocked;
}

export function markUnlocked() {
    sessionUnlocked = true;
}
