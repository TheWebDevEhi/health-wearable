# Keis Band — Companion App

Web Bluetooth PWA for the BEME upper-arm wearable. See
[../readme.md §6](../readme.md#6-communication) for the hardware side of
this contract, and [DEVELOPMENT.md](DEVELOPMENT.md) here for how this code
is put together.

## Running it locally

No build step — plain ES modules served as static files.

```bash
cd companion-app
python -m http.server 8000
```

Then open `http://localhost:8000` in **Chrome or Edge** (desktop or
Android). Web Bluetooth requires a secure context, but `http://localhost` is
exempt from the HTTPS requirement — a real deployment needs HTTPS.

**Not supported:** Safari and Firefox don't implement Web Bluetooth at all
(including iOS, where no browser can — it's a WebKit restriction, not a
Safari-specific one). The app detects this and shows a message instead of a
broken Connect button.

## Requirements to actually test the connect flow

The ESP32-S3 needs to be flashed and advertising — this app has no mock/fake
device mode. UI and decoder logic can be exercised without hardware (see
`js/decoders.js`, which is pure and DOM-free), but `js/ble.js` cannot be.

## Status

Scaffolded, not yet run against real hardware: connect flow, all six live
characteristics (heart rate, SpO2, temperature, battery, steps, flags), and
history backfill are implemented for real. Settings is functional: device
rename, Wi-Fi/OTA credentials (behind a client-side admin-password prompt —
see [DEVELOPMENT.md](DEVELOPMENT.md#the-admin-password-is-a-deterrent-not-security)
for what that does and doesn't protect against), and a dark/light theme
toggle.
