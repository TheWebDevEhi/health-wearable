# Development Notes — Companion App

Conventions for this directory specifically. The firmware's
[../DEVELOPMENT.md](../DEVELOPMENT.md) covers the C++ side; this file is the
same idea for the web app, since the two codebases have different enough
conventions (JS modules vs. C++ classes) that one shared doc would blur
together.

## Conventions

### No build step, on purpose

Plain ES modules loaded via `<script type="module">`, no bundler, no
framework — matches [readme.md §9](../readme.md#9-firmware-layout)'s
"plain `navigator.bluetooth` ... plus a light charting library." `npm` only
enters the picture if a real charting library gets added later; until then,
adding a build step would be pure overhead for four small files.

### Layered by responsibility, not by screen

- `js/uuids.js` — the single source of truth for every service/characteristic
  UUID, mirroring `config.h`'s role on the firmware side. If a UUID ever
  changes on the firmware, this is the only file that should need to change
  here.
- `js/decoders.js` — pure functions, `DataView` in, plain value out. No BLE
  or DOM dependency, so they're unit-testable against synthetic byte arrays
  without any hardware (no test runner is wired up yet, but the functions
  are already shaped for it).
- `js/ble.js` — owns the `BluetoothDevice`/`BluetoothRemoteGATTServer`
  lifecycle and republishes decoded values as `CustomEvent`s on an
  `EventTarget`. Knows nothing about the DOM.
- `js/app.js` — the only file allowed to touch `document.*`. Subscribes to
  `BleClient`'s events and updates the DOM; owns no BLE or decoding logic
  itself.
- `js/settings-protocol.js` — the write-side mirror of `decoders.js`: pure
  functions, plain values in, `Uint8Array` payload out. Same reasoning —
  testable without BLE, and the one place that has to stay byte-for-byte in
  sync with the firmware's parser.
- `js/admin-auth.js` — the client-side password-hash gate. Pure state
  (localStorage + one module-level flag), no DOM.
- `js/password-dialog.js` — the only file that touches the
  `<dialog id="password-dialog">` markup. A thin promise wrapper so
  `app.js` can `await promptPassword(...)` instead of juggling dialog
  events itself.
- `js/theme.js` — reads/writes the `data-theme` attribute + localStorage.
  The *first* paint's theme is set by an inline script in `index.html`'s
  `<head>` (has to run before CSS/module loading to avoid a flash); this
  module is for changing it afterward via the Settings dropdown.

Keep new features in the layer they belong to rather than reaching across —
e.g. a new characteristic gets a UUID constant, a decoder function, a
`_wireCharacteristic()` call in `ble.js`, and a DOM update in `app.js`, in
that order.

### Matching the firmware's wire formats exactly

Every decoder in `decoders.js` has a comment pointing at the exact function/
struct in `src/comms/ble_gatt.cpp` it must match. If that firmware code
changes format, the decoder here must change with it in the same PR/commit —
these two files are effectively one interface split across two languages.
The Health Thermometer decoder is the one most likely to be copy-pasted
wrong elsewhere: it's IEEE-11073 32-bit FLOAT, not IEEE754 `float32`.

### No auto-reconnect

[readme.md §6](../readme.md#6-communication) requires a fresh user tap for
every connection and accepts the link dropping on tab close. `BleClient`
does not attempt to reconnect on `gattserverdisconnected` — don't add that
without checking the spec first, it's a deliberate constraint, not a missing
feature.

### The admin password is a deterrent, not security

`admin-auth.js` gates Wi-Fi/OTA credential changes behind a password whose
SHA-256 hash lives in `localStorage`, checked entirely client-side. This was
an explicit, scoped request ("a simple frontend side admin password"), not
an attempt at real access control — see the block comment at the top of
that file before extending it. The firmware doesn't know this password
exists and doesn't check anything on its end (see the firmware's
DEVELOPMENT.md "Settings wire protocol"). Don't let this gate creep into
looking more authoritative than it is (e.g. don't start calling it "login"
or storing anything that implies a real account system) — it's one
`localStorage.getItem()` away from being bypassed by anyone with devtools.

### Theme

Two-part, deliberately: an inline, dependency-free script at the top of
`index.html`'s `<head>` sets `data-theme` before first paint (avoids a
flash of the wrong theme while `js/app.js` loads as a module — module
scripts are deferred, which would otherwise mean a visible flash), and
`js/theme.js` handles changing it afterward from the Settings dropdown. The
theme is a companion-app-only preference — the device itself has no concept
of a "theme," so this never touches BLE.

### Comments

Same rule as the firmware side: no comments restating what the code does.
Only WHY, a TODO tied to a specific readme.md section, or a non-obvious
constraint.

## Scaffolding log

**2026-09-09 — Initial scaffold.** Created the full structure: `uuids.js`,
`decoders.js` (real decode logic for all six live characteristics plus
history entries, not stubs — the wire formats are already fully specified by
the firmware, so there was nothing to leave as a TODO), `ble.js` (real
connect flow, one service/characteristic per constant in `uuids.js`), and
`app.js` (DOM wiring: tiles, alert banner, a canvas sparkline for the
history backfill). Plus the PWA shell (`manifest.json`,
`service-worker.js`, an SVG icon) and this directory's own README/
DEVELOPMENT split, mirroring the firmware's.

Not yet done: no real device to test the connect flow against (same
situation as the firmware itself — see the parent DEVELOPMENT.md); no
Settings UI, since the firmware has no settings-write wire format yet; no
test runner wired up for `decoders.js` even though the functions are already
shaped for one; charting is a hand-rolled canvas sparkline rather than a
real library, since readme.md only asked for "a light charting library" and
one wasn't picked yet.

**2026-09-09 — Rename + real Settings section.** Renamed "BEME Band" to
"Keis Band" everywhere in this directory (title, manifest, connect-screen
copy, service worker cache name bumped to `keis-band-shell-v2` so stale
caches from the old name don't linger). Replaced the Settings placeholder
with a working section, matching the firmware's new settings write support:

- `js/settings-protocol.js` — encoders for both settings commands, verified
  byte-for-byte against a JS reimplementation of the firmware's parser
  (round-tripped device name + Wi-Fi credentials through both, including
  the length-cap rejection) before trusting it.
- `js/admin-auth.js` + `js/password-dialog.js` — the admin-password gate
  and its `<dialog>`-based UI, used only for the Wi-Fi/OTA credentials
  save button (device name and theme are ungated, per how the request was
  scoped).
- `js/theme.js` + the inline head script in `index.html` — dark/light
  toggle, verified in-browser to actually repaint (computed
  `background-color` checked, not just the stored preference value).
- New `.setting-row`/`.setting-group`/`dialog` CSS, using the same
  `--bg`/`--panel`/`--text` custom properties as everything else, so the
  light theme "just worked" for these new elements with no extra
  theme-specific rules needed.

Found and fixed one real bug via in-browser testing: `.setting-button`'s
`width: auto` was losing to the earlier `button.secondary { width: 100% }`
rule on CSS specificity (`button.secondary` = element+class beats a
class-only selector, regardless of source order), which squeezed the
device-name input down to nothing next to an oversized Save button. Fixed
by matching the selector shape (`button.setting-button`) rather than fixing
it with `!important`. Also hit repeated stale-service-worker content during
manual testing (expected PWA dev friction, not a code bug) — had to
unregister + clear caches between edits to see current output; not an issue
for actual users since a real deploy doesn't edit files out from under a
running service worker.

Not yet done: same hardware blocker as always — no real device to actually
send a settings write to. The on-device Settings screen (drawn on the
ST7735) still isn't interactive; only this app's Settings section is.

**2026-09-09 — Fixed the history-backfill race (bug #2 from an end-to-end
review).** `ble.js`'s `connect()` used to rely on the firmware pushing
history automatically right after connect — but the firmware's `onConnect`
fires before this file has finished its sequential `_wireCharacteristic()`
calls (history was the 7th of 7), so the notifications were being sent
before anything had subscribed and silently dropped. Real, not
hypothetical: traced the actual timing on both sides, not just inferred it.

Fixed by adding `CMD_REQUEST_HISTORY` (`0x03`) to `settings-protocol.js`
and a `requestHistory()` method to `BleClient`, called as the last step of
`connect()` — after every characteristic, history included, is subscribed.
The firmware side dropped its automatic push entirely and now only sends
history in response to this command (see the firmware's DEVELOPMENT.md
"Settings wire protocol"). Verified `encodeRequestHistory()` produces the
single byte the firmware parser expects, same in-browser round-trip
approach as the other two settings commands.

The two other bugs from that review (status LED never blinking, and the
band deep-sleeping out from under an active connection) were fixed entirely
on the firmware side — nothing changed here for those.

**2026-09-09 — Fixed the remaining findings that touch this app.**

- **Unbounded history array.** The `connected` event handler in `app.js`
  now does `history.length = 0` (plus a `drawHistory()` to clear the
  sparkline) before setting the connected UI state. Without this, every
  reconnect appended another full backlog on top of whatever was already
  there — unbounded growth over a long session, and a sparkline mixing
  entries from different device boot epochs (the firmware's `ageMs` resets
  to 0 on every reboot from deep sleep, but nothing here was ever clearing
  the old ones out). Safe to clear right on `connected` rather than waiting
  for the first `historyEntry`: `ble.js`'s `connect()` sends the history
  request *before* emitting `connected`, and the resulting notifications
  can only arrive after that write's round-trip completes, so this ordering
  reliably runs first in practice.
- **OTA trigger.** Added `encodeStartOta()` (`0x04`, no payload) to
  `settings-protocol.js` and a `startOtaUpdate()` method to `BleClient`.
  The Settings section gained a "Firmware update" group with a "Start
  update" button, gated behind the same `requireAdminUnlock()` flow as the
  Wi-Fi credentials save — triggering a firmware update is at least as
  sensitive as changing Wi-Fi credentials, so it gets the same deterrent.
  Verified `encodeStartOta()` produces `[0x04]`, matching the firmware's
  `kCmdStartOta`, and manually walked the full click → admin-unlock →
  BLE-write-attempt path in-browser (fails gracefully with no device
  connected, as expected — same verification depth as the other settings
  actions).

The other two fixes from this round (`BleGatt` mutex, unchecked sensor
`begin()`) were entirely firmware-side — nothing changed here for those.
