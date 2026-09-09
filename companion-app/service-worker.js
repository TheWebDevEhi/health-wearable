// Caches the static app shell only — never the BLE connection or live
// values, which obviously can't be cached. Lets the PWA install and open
// (to the connect screen) even with no network, which matters here since
// the whole point of the app is talking to a nearby device, not a server.

const CACHE_NAME = "keis-band-shell-v2";
const SHELL_FILES = [
    "./",
    "index.html",
    "manifest.json",
    "css/app.css",
    "js/app.js",
    "js/ble.js",
    "js/decoders.js",
    "js/uuids.js",
    "js/settings-protocol.js",
    "js/admin-auth.js",
    "js/password-dialog.js",
    "js/theme.js",
    "icons/icon.svg",
];

self.addEventListener("install", (event) => {
    event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(SHELL_FILES)));
    self.skipWaiting();
});

self.addEventListener("activate", (event) => {
    event.waitUntil(
        caches
            .keys()
            .then((keys) => Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key)))),
    );
    self.clients.claim();
});

self.addEventListener("fetch", (event) => {
    event.respondWith(caches.match(event.request).then((cached) => cached || fetch(event.request)));
});
