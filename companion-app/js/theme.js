// Dark/light theme, persisted locally — purely a companion-app preference,
// no BLE round-trip (the device has no concept of a "theme"). The actual
// zero-flash initial paint is handled by the inline script in index.html's
// <head>, which runs before this module and before first paint; the
// functions here are for the Settings dropdown to read/change it afterward.

const STORAGE_KEY = "keis-theme";

export function getStoredTheme() {
    return localStorage.getItem(STORAGE_KEY);
}

export function applyTheme(theme) {
    document.documentElement.setAttribute("data-theme", theme);
}

export function setTheme(theme) {
    localStorage.setItem(STORAGE_KEY, theme);
    applyTheme(theme);
}

export function currentTheme() {
    return document.documentElement.getAttribute("data-theme") || "dark";
}
