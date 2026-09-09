---
name: verify
description: Drive the Keis Band companion app's real browser surface for manual verification.
---

# Verifying the companion app

No build step. Serve the directory and drive it in a real browser.

## Launch

```bash
python -m http.server 8471 --directory companion-app
```

Open `http://localhost:8471` in the Browser pane (or any Chrome/Edge).

## Gotcha: stale service worker

The app registers a service worker on every load (`js/app.js`) that caches
the whole shell (`service-worker.js`, cache name bumped on each shape
change, currently `keis-band-shell-v2`). If you've tested this app in the
same browser profile before, you WILL see stale JS/CSS even after editing
files on disk. Always clear before a verification pass:

```js
const regs = await navigator.serviceWorker.getRegistrations();
for (const r of regs) await r.unregister();
const keys = await caches.keys();
for (const k of keys) await caches.delete(k);
localStorage.clear(); // also drops any admin password hash / theme from prior runs
```
...then navigate again (the clear only takes effect on the *next* load).

## The BLE wall

`navigator.bluetooth.requestDevice()` opens a real native device chooser —
this environment has the API (no "unsupported" message) but no Bluetooth
adapter/devices, so it resolves as `NotFoundError: User cancelled the
requestDevice() chooser`. This is genuine, correct behavior to observe once
(confirms the app calls the real API and handles rejection gracefully,
re-enabling the Connect button) — but it's a hard wall. Nothing past actual
device pairing is reachable without physical hardware.

**To verify anything past that wall** (the dashboard, Settings section),
bypass only the connect gate via direct DOM state — nothing else:

```js
document.getElementById('connect-screen').hidden = true;
document.getElementById('dashboard').hidden = false;
```

Everything from there — theme toggle, device-name save, Wi-Fi credentials
save, OTA trigger, the admin-password dialog — is wired to real DOM
elements and real click/change handlers, so drive it with actual
clicks/typing (`computer` tool / `form_input`), not further JS pokes. The
`ble` instance and `history` array are function-scoped inside `app.js`'s
`main()`, not exposed on `window` — there's no way to fake a `connected` or
`historyEntry` event from outside without editing the shipped file, so the
history-clearing-on-reconnect behavior specifically cannot be driven this
way; that one needs a real device.

## Known trap: settings-write error messages while disconnected

With `settingsCharacteristic` still `null` (the state you're in after the
bypass above), `this.settingsCharacteristic.writeValueWithResponse(encode___(x))`
throws on the property lookup (`Cannot read properties of null...`)
*before* JS ever evaluates the argument — so `encodeDeviceName`'s/
`encodeWifiCredentials`'s own validation (name length, SSID length, etc.)
never actually runs in this disconnected test state. Don't mistake that
generic null error for "validation is broken." If you need to confirm the
validation logic itself, call the pure encoder functions from
`settings-protocol.js` directly with the same input — that's legitimate for
checking pure validation logic specifically, just don't mistake it for
having driven the write path.

## Worth re-checking each pass

- Theme toggle actually repaints (`getComputedStyle(document.body).backgroundColor`,
  not just the stored preference)
- Admin password: mismatch on first-set, wrong password on a later session,
  cancel mid-dialog — all via the real `<dialog>`, not by calling
  `admin-auth.js` functions directly
- Session-unlock persists across Wi-Fi-save and OTA-start in the same tab
  session but not across a reload
