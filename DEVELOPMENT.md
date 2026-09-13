# Development Notes

Conventions and a running log of firmware scaffolding decisions for the BEME
upper-arm wearable, so later additions stay consistent with what's already
here. Hardware/behavioural specs live in [readme.md](readme.md); this file
is about *how the code is put together*, not what the device does.

---

## Conventions

### File and directory layout

- Follow the tree in [readme.md §9](readme.md#9-firmware-layout). New modules
  go under the matching subdirectory (`src/sensors`, `src/display`,
  `src/comms`, `src/power`); anything that doesn't belong to one sensor,
  screen, or comms path lives directly under `src/`.
- One `.h` / `.cpp` pair per module, same base name (e.g. `motion_lis3dh.h` /
  `motion_lis3dh.cpp`). Filenames are `snake_case`.
- Include guards: `#pragma once` — no manual `#ifndef` guards.

### Naming

- Classes: `PascalCase`, named after the file (`motion_lis3dh.*` →
  `MotionLis3dh`).
- Methods/variables: `camelCase`. Private member fields: `_camelCase`.
- Constants and macros: `SCREAMING_SNAKE_CASE`, centralized in
  [src/config.h](src/config.h) — pin numbers, I2C addresses, alert limits,
  and task stack/priority values. Don't hardcode a GPIO number or I2C address
  anywhere else; add it to `config.h` and reference the constant.

### Module shape

Each sensor/display/comms/power class exposes:

- `bool begin(...)` — one-time init, returns whether the underlying driver
  came up.
- `void update(...)` — called on a tick from the owning task; does the actual
  work (read a sensor, redraw, notify BLE). Never blocks longer than its own
  sample/render budget.
- Plain getters for the last-known values (no getters that trigger a fresh
  read — that belongs in `update()`).

This mirrors the FreeRTOS task diagram in
[readme.md §9](readme.md#9-firmware-layout): each task owns a fixed tick and
one job, and publishes results rather than being polled synchronously by
another task.

### Shared data store

[src/sensor_data.h](src/sensor_data.h) defines `SensorSnapshot`, a
mutex-guarded, copy-in/copy-out store: the sensor task is the only writer,
every other task reads via `sensorDataGet()`. This is the "shared sensor data
store" node in the README's task diagram — it wasn't in the original `src/`
tree, so it's called out here explicitly. Don't hand out a pointer/reference
into the store or take the mutex directly from outside `sensor_data.cpp`;
add a new field + accessor pattern instead if a task needs something the
snapshot doesn't carry yet.

### Bus ownership

I2C (`Wire`) and SPI are each initialized once, in `setup()` in
[src/main.cpp](src/main.cpp), on the pins from `config.h`. Sensor/display
constructors take the bus by reference (or, for XPT2046, the CS/IRQ pins
directly) rather than calling `Wire.begin()`/`SPI.begin()` themselves — all
four sensors share one I2C bus and the display+touch share one SPI bus
(readme.md §8), so only one place should configure either bus.

### Task model

Tasks are created in `setup()` with `xTaskCreate` (no core pinning yet —
`tskNO_AFFINITY`; revisit once real timing data from bring-up says otherwise).
Stack sizes and priorities are named constants in `config.h`, not inlined at
the call site. Current priorities (FreeRTOS scale, higher = more urgent; the
Arduino core's own `loopTask` runs at priority 1):

| Task    | Priority | Reasoning                                   |
| ------- | -------- | -------------------------------------------- |
| Sensor  | 3        | Timing-sensitive PPG sampling                |
| Power   | 3        | Must respond promptly to wake/sleep triggers |
| BLE     | 2        | Notifications tolerate some jitter           |
| Display | 1        | Least time-critical                          |
| OTA     | 1        | Rare, long-blocking (Wi-Fi/HTTP) — must never share a task with anything time-sensitive |

### Logging

Prefix `Serial` output with the module name in brackets, e.g.
`Serial.printf("[BLE] client connected\n")`. No logging macro/framework yet —
add one only if plain `Serial.printf` stops being enough (e.g. a need to
compile logging out of release builds).

### Library pins (`platformio.ini`)

`lib_deps` uses caret ranges (`^x.y.z`) with a conservative floor rather than
exact pins, so `pio run` picks up patch/minor fixes automatically. Once
hardware bring-up starts, lock each library to the exact version actually
tested against and note why in a commit message if a newer version breaks
something.

TFT_eSPI is configured entirely through `build_flags` in `platformio.ini`
(`USER_SETUP_LOADED=1` plus the `TFT_*`/`ST7735_*` defines) instead of
editing the library's `User_Setup.h`, so a `pio pkg update` never silently
reverts the pin map back to the library's default.

### Versioning

`FIRMWARE_VERSION` in `src/config.h` and the `**Version:**` line at the top
of `readme.md` move together — bump both in the same commit.

### Secrets

Never commit real credentials, even as a "temporary" placeholder edit.
Wi-Fi/OTA settings live in `src/credentials.h`, which is gitignored; the
tracked template is `src/credentials.example.h`. To set them up:

```bash
cp src/credentials.example.h src/credentials.h
# then edit src/credentials.h with real values
```

`wifi_sync.cpp` includes `"../credentials.h"` and nothing else references
these constants — if a future module needs its own secret, add it to both
files rather than hardcoding it inline. `src/credentials.h` ships in this
repo pre-seeded with the same `TODO_*` placeholders as the example, purely
so the project builds out of the box; it still needs real values before OTA
can work.

### Windows build path workaround

Some libraries' own example filenames (e.g. SparkFun MAX3010x's
`Example7_Basic_Readings_Interrupts.ino`) push the full path past Windows'
260-char `MAX_PATH` once nested under this project's directory, which is
already deep (a git worktree under `.claude/worktrees/...`). `platformio.ini`
redirects `libdeps_dir`/`build_dir` to `${sysenv.USERPROFILE}\.pio-libdeps`
and `\.pio-build` — short, user-portable paths outside the project — instead
of relying on Windows long-path support being enabled system-wide. If
`pio run` ever fails with `WinError 3`/`[Errno 2]` during a library install,
this is why; don't move the project itself to fix it, the ini already
routes around it.

### Alert detection is one function, not one per caller

`sensorDataIsAlert()` in `sensor_data.h`/`.cpp` is the single place that
decides whether a `SensorSnapshot` counts as an alert (readme.md §4's
thresholds from `config.h`). Both the display task (to switch to the Alert
screen) and `BleGatt::notify()` (to set the alert-flags bit) call it rather
than re-deriving the same conditions — if you add a new alert condition, add
it there once. `UiScreens::renderAlert()` still lists individual reasons for
the on-screen text and must stay logically in sync with it (mirrored, not
shared, since one returns a single bool and the other builds a list of
strings).

### The `dataValid` gate

Every `SensorSnapshot` field defaults to `0`/`false`, and `0` is itself a
false alert trigger for several fields (temp/battery read as critically low
before the first real sample). `SensorSnapshot::dataValid` is set once the
sensor task completes its first cycle; `sensorDataIsAlert()` and
`BleGatt::notify()` both return early while it's false. Any new consumer of
`SensorSnapshot` that makes a threshold decision needs the same guard.

### BLE UUIDs

The custom "Motion & Control" service and its five characteristics
(SpO2, steps, flags, history, settings-write) use randomly generated 128-bit
UUIDs, defined as string literals directly in `ble_gatt.cpp::begin()` —
they're not from any BLE SIG spec, readme.md doesn't pin specific values,
and once a companion app is built against them they must not change. Standard
services (Heart Rate `0x180D`, Health Thermometer `0x1809`, Battery
`0x180F`) and their characteristics use the real BLE SIG 16-bit UUIDs and
wire formats — including the Health Thermometer's IEEE-11073 32-bit FLOAT
encoding for temperature, which is not a plain `float` cast.

SpO2 specifically has no live path through a standard service: the BLE SIG's
Pulse Oximeter Service (`0x1822`) exists but its measurement characteristic
is a heavier multi-field IEEE-11073 structure (SpO2 + pulse rate + status
flags), not worth the complexity here. It's instead a plain
`NIMBLE_PROPERTY::READ | NOTIFY` uint8 percent characteristic in the custom
service (UUID `a8fb0a1c-eb12-47a6-8ffb-d65d1dc4eaeb`), same shape as Battery
Level — added after the fact once it became clear the original GATT table in
readme.md §7 listed SpO2 as a device feature but never assigned it a live
BLE path (only the history buffer carried it). If you add another vital that
has no clean standard-service fit, follow this pattern rather than adopting
a heavier spec service just for the sake of "standard."

### Settings persistence (NVS)

`SettingsStore` (`src/settings_store.*`) holds the device name and Wi-Fi
credentials — the only settings that can be changed at runtime so far (via
BLE writes, see below). It persists them in NVS through the `Preferences`
library rather than a plain member variable, because the device deep-sleeps
regularly (`power/power_mgr.*`), which wipes ordinary RAM; anything that
must survive that has to live somewhere else. Namespace is `"keis"`, keys
are `"name"`/`"ssid"`/`"pass"`. Falls back to `config.h`'s
`DEFAULT_DEVICE_NAME` and `credentials.h`'s placeholders until the app has
actually written something — those two files remain the compile-time
defaults, `SettingsStore` is what can override them without reflashing.

If you add another runtime-editable setting, follow this same shape: a
getter/setter pair on `SettingsStore` that reads from a RAM cache but writes
through to NVS immediately, not a value that only lives on the stack of
whatever function received it.

### Settings wire protocol

The settings characteristic (custom UUID, `NIMBLE_PROPERTY::WRITE`) accepts
a small tagged binary protocol, parsed in
`BleGatt::SettingsCallbacks::onWrite()`:

| Byte(s) | Meaning |
| --- | --- |
| `[0]` | command: `0x01` = set device name, `0x02` = set Wi-Fi credentials, `0x03` = request history, `0x04` = start OTA update |
| **Command 0x01** | `[1..]` = UTF-8 name, 1-20 bytes |
| **Command 0x02** | `[1]` = ssidLen (uint8) · `[2..2+ssidLen)` = SSID · next byte = passLen (uint8) · following passLen bytes = password |
| **Command 0x03** | no payload — triggers `sendHistoryBacklog()` immediately |
| **Command 0x04** | no payload — sets a flag `otaTask` (in `main.cpp`) polls once a second |

Command `0x03` exists because history used to be pushed automatically from
`ServerCallbacks::onConnect()`, which fires before a client has finished
GATT-subscribing to the history characteristic — the notifications were
being sent to no subscriber and silently dropped. The companion app now
sends `0x03` as the last step of its connect flow, once every characteristic
(history included) is actually subscribed; see `ble.js`'s `requestHistory()`.

Command `0x04` deliberately does *not* call `WifiSync::startOtaUpdate()`
directly from `onWrite()` — that runs on NimBLE's own host task, and
`startOtaUpdate()` blocks for a Wi-Fi connect timeout plus however long an
actual download takes, which would stall BLE entirely for that whole span.
Instead it just sets `BleGatt::_otaRequested` (under `_mutex`, same as
everything else shared across tasks in this class);
`BleGatt::consumeOtaRequest()` test-and-clears it, polled once a second by a
new, dedicated `otaTask` in `main.cpp` — see "Task model" above for why it's
its own task rather than folded into an existing one.

Length caps (`kMaxDeviceNameLen`/`kMaxSsidLen`/`kMaxPasswordLen` in
`ble_gatt.cpp`) are 20/32/64 bytes — WPA2's real max password length is 63
bytes, rounded up to 64. `NimBLEDevice::setMTU(247)` is set in `begin()` so
the largest possible Wi-Fi-credentials write (1+1+32+1+64 = 99 bytes) fits
in one ATT write without needing reassembly logic this code doesn't have —
the default 23-byte MTU would not have been enough.

There is **no authentication on this BLE write** — anyone within BLE range
who connects can write these commands. The "admin password" the companion
app prompts for before sending Wi-Fi credentials is a client-side-only gate
(see `companion-app/js/admin-auth.js`); the firmware doesn't know it exists
and doesn't check anything. If that gap matters for a real deployment,
NimBLE's `NIMBLE_PROPERTY::WRITE_ENC` (requiring pairing/bonding) is the
real fix — it wasn't added here because it's a materially bigger feature
than what was asked for, not because it was overlooked.

A device name change calls `NimBLEDevice::setDeviceName()` +
`advertising->setName()` and restarts advertising so the new name shows up
in scans immediately; a Wi-Fi credentials change just persists — it takes
effect on the next OTA attempt since Wi-Fi is only brought up on demand
(readme.md §7), so there's nothing to reconnect immediately.

The companion app's encoder (`companion-app/js/settings-protocol.js`) must
stay byte-for-byte in sync with this table — see that file's own
DEVELOPMENT.md for the JS side.

### Button semantics

readme.md §4 says the two side buttons are primary and doesn't specify what
each does. `main.cpp::pollButtons()` picks: Button 1 cycles
Home → Detail → Settings → Home; Button 2 cycles the Detail screen's metric,
or jumps to Home from elsewhere. This is an implementation choice, not a
spec — expect it to change once there's a real device to try it on.

### Comments

Same rule as the rest of this project: no comments that restate what the
code does. A comment is only for a non-obvious WHY, a TODO tied to a specific
readme.md section, or a constraint that would otherwise surprise a reader
(e.g. why Button 2 isn't a wake source).

---

## Scaffolding log

**2026-09-08 — Initial scaffold.**
Created `platformio.ini` and the `src/` tree from
[readme.md §9](readme.md#9-firmware-layout):

- `platformio.ini` — `esp32-s3-devkitc-1` board, Arduino framework, TFT_eSPI
  pin config via `build_flags`, and `lib_deps` for all libraries named in the
  README (TFT_eSPI, XPT2046_Touchscreen, SparkFun MAX3010x, Adafruit
  MLX90614, Adafruit LIS3DH + Unified Sensor, Adafruit MAX1704X,
  NimBLE-Arduino). Registry/GitHub identifiers confirmed via web search
  against the PlatformIO registry rather than guessed.
- `src/config.h` — single source of truth for the pin map (readme.md §8.3),
  I2C addresses (§8.4), placeholder alert limits (§4, marked TODO — not yet
  tuned against real data), and task stack/priority constants.
- `src/sensor_data.{h,cpp}` — the shared, mutex-guarded sensor store implied
  by the README's FreeRTOS task diagram. Added beyond the literal §9 tree
  because the task model doesn't work without it; see "Shared data store"
  above.
- `src/sensors/*` — one stub class per sensor (`PpgMax30102`, `TempMlx90614`,
  `MotionLis3dh`, `FuelMax17048`) with `begin()`/`update()`/getters; driver
  logic left as TODOs referencing the relevant readme.md section.
- `src/display/*` — `Screen` (TFT_eSPI wrapper), `TouchXpt2046`, and
  `UiScreens` (Home/Detail/Alert/Settings, matching readme.md §4).
- `src/comms/*` — `BleGatt` and `WifiSync` stubs matching the GATT services
  and OTA-on-demand behaviour in readme.md §7.
- `src/power/*` — `PowerMgr`, with the EXT1 deep-sleep wake sources
  (LIS3DH INT1 on GPIO1, Button 1 — originally GPIO21, reassigned to GPIO3
  on 2026-09-11, see the dated entry below) wired for real since they're
  fixed by the RTC-GPIO hardware constraint (readme.md §5, §8.3); the idle
  timeout logic itself is still a TODO.
- `src/main.cpp` — bus init, module `begin()` calls, and the four FreeRTOS
  tasks (sensor/display/BLE/power) from readme.md §9.

Not yet done: any actual driver/algorithm implementation (everything under
`update()` is a stub), TFT_eSPI tab variant confirmation, PSRAM confirmation,
and the bring-up checklist in readme.md §11 — all pending real hardware.

**2026-09-09 — Foundation-to-top implementation pass.**
Replaced the stubs with real driver logic, one layer at a time, compiling
against the actual ESP32-S3 toolchain after each layer (PlatformIO Core
6.1.19 is installed in this environment) rather than waiting until the end —
caught real bugs early this way (see below). Order followed the dependency
chain: sensors first (motion before PPG, since PPG needs motion's gating
output), then display, then power, then BLE/Wi-Fi, then main.cpp wiring.

- **Sensors** — `MotionLis3dh` (Adafruit_LIS3DH: `begin()`, `setRange()`,
  `setClick(2, threshold)` for the double-tap wake engine, magnitude-based
  movement detection; step-counting and fall detection still TODO — real
  algorithms, not just missing plumbing). `TempMlx90614` (Adafruit_MLX90614,
  `readObjectTempC()` + the config.h offset, 4-sample moving average).
  `FuelMax17048` (Adafruit_MAX17048 — note the library package is "Adafruit
  MAX1704X" but the class is `Adafruit_MAX17048`; `cellPercent()` against a
  placeholder low-battery threshold). `PpgMax30102` (SparkFun's library
  class is `MAX30105`, covers the whole MAX3010x family; ring-buffer
  drained non-blockingly each tick rather than the reference example's
  blocking `while(available()==false) check();` loop, since this runs on a
  shared FreeRTOS task tick — see the comment in `ppg_max30102.cpp`. HR/SpO2
  via the bundled `spo2_algorithm.h` ratio-of-ratios function). All four
  library APIs were verified against real source/examples (GitHub fetches,
  not memory) before writing code that calls them — see the PlatformIO
  registry search log in the previous entry for why that mattered.
- **Display** — `Screen`/`TouchXpt2046` were already close to real from the
  scaffold. Verified (by reading arduino-esp32's `SPI.cpp` source) that
  `TouchXpt2046::begin()`'s internal `SPI.begin()` call is a safe no-op once
  `main.cpp` has already called `SPI.begin()` with the shared bus's custom
  pins — `SPIClass::begin()` returns early if the bus is already
  initialized, so it won't reset the pins to hardware defaults. Documented
  that ordering dependency in `touch_xpt2046.h` rather than leaving it
  implicit. `UiScreens` now does real TFT_eSPI drawing for all four screens,
  including a small ring-buffer trend line on the Detail screen. Added
  `StatusLed` (Adafruit NeoPixel, not in the original §9 tree — see "Shared
  data store" entry above for the pattern of calling out such additions) for
  the WS2812 alert-blink behaviour in readme.md §4.
- **Power** — `PowerMgr` now does a real idle-timeout → backlight-off →
  `esp_deep_sleep_start()` sequence (`IDLE_TIMEOUT_MS` in config.h), wired to
  a `Screen` reference via a new `attachScreen()` (Screen isn't constructed
  yet when `PowerMgr::begin()` runs in `main.cpp`'s setup order).
- **BLE** — Real NimBLE-Arduino 2.x GATT server: standard Heart Rate
  (`0x180D`), Health Thermometer (`0x1809`, with correct IEEE-11073 32-bit
  FLOAT temperature encoding — not a raw float cast), and Battery (`0x180F`)
  services, plus the custom "Motion & Control" service (see "BLE UUIDs"
  above). Connect/disconnect tracked via `NimBLEServerCallbacks`; a rolling
  history ring buffer (60 entries, ~1 min at the BLE task's 1 Hz tick) is
  pushed on every `notify()` and drained to the client on connect, matching
  readme.md §6's reconnect-backfill behaviour. Verified the NimBLE-Arduino
  2.x callback signatures and `createService`/`createCharacteristic` API
  against the actual installed version (2.5.1) via the library's own
  example, since 1.x→2.x changed callback signatures. Removed
  `NimBLEService::start()` calls after the first real build flagged them as
  deprecated (2.x auto-starts services with the server).
- **Wi-Fi/OTA** — `WifiSync` now does a real connect → `httpUpdate.update()`
  → disconnect flow. SSID/password/update URL are left as clearly-marked
  `TODO_*` placeholders in `wifi_sync.cpp` — no OTA server or network exists
  for this project yet, and inventing one would be worse than an honest gap.
- **main.cpp** — wired buttons (polled in the display task, see "Button
  semantics" above), the status LED, and alert-screen override (via
  `sensorDataIsAlert()`) together; set `SensorSnapshot::dataValid` at the end
  of the sensor task's first cycle (see "The `dataValid` gate" above).

Real bugs caught by compiling incrementally instead of all at once:
`power_mgr.h` used `uint32_t` without including `<cstdint>`; `ble_gatt.cpp`
used `millis()`/`Serial` without including `<Arduino.h>`; and — before any of
the above — the scaffold itself failed to build at all on Windows because of
the `MAX_PATH` issue (see "Windows build path workaround"). None of these
would have been caught by writing all the code first and compiling once at
the end.

Final build: 605,897 bytes flash (18.1%), 34,048 bytes RAM (10.4%) on the
`esp32-s3-devkitc-1` target, zero warnings from project code (the SparkFun
MAX3010x library redefining `I2C_BUFFER_LENGTH` against an ESP32 core macro
is a third-party warning, not ours to fix).

Still not done: no physical hardware to run any of this on yet — everything
above is "compiles and is logically real" but unverified against actual
sensors, and the bring-up checklist in readme.md §11 (ST7735 tab, PSRAM,
double-tap tuning, etc.) is unstarted. Also open: step-counting/fall-detection
algorithms, touch-driven UI interaction (buttons are wired, touch reads but
isn't hooked to anything yet), Settings-screen interactivity, and the BLE
settings-write wire format.

**2026-09-09 — Rename + real settings.** Two changes, prompted by the user:
the BLE device name changed from "BEME Band" to "Keis Band"
(`DEFAULT_DEVICE_NAME` in `config.h`), and the settings characteristic went
from a logging-only stub to a real, persisted protocol.

- `src/settings_store.*` — new module, NVS-backed via `Preferences` (see
  "Settings persistence (NVS)" above). Added because device name and Wi-Fi
  credentials both need to survive deep sleep, which a RAM-only value
  wouldn't.
- `BleGatt` — added `attachSettingsStore()` (called before `begin()`, since
  the initial advertised name comes from there), the settings write parser
  (see "Settings wire protocol" above), `applyDeviceName()` (renames live +
  restarts advertising), and `applyWifiCredentials()` (persists; takes
  effect on next OTA attempt). Also widened the BLE MTU to 247 — the
  previous default (23 bytes) wasn't enough for a full-length Wi-Fi
  credentials write, which the settings protocol now actually needs to
  carry.
- `WifiSync` — reads Wi-Fi credentials from `SettingsStore` each connection
  attempt instead of the `credentials.h` constants directly;
  `credentials.h` is now only the fallback default, not the only source.
- `main.cpp` — instantiates and wires `SettingsStore` before anything that
  depends on it.

Deliberately out of scope, and documented as such rather than silently
skipped: the BLE settings write itself has no authentication — the
companion app's "admin password" is a client-side-only deterrent (see
DEVELOPMENT.md there), not something the firmware knows about or enforces.
Real protection would mean `NIMBLE_PROPERTY::WRITE_ENC` and a
pairing/bonding flow, which wasn't part of what was asked for.

Rebuilt clean after this change (NimBLE recompiled from scratch both times
in this session for reasons not fully understood — possibly cache
invalidation from the new `Preferences`/NVS/filesystem dependencies pulling
in different build flags; harmless, just slower iteration, ~5 minutes per
full rebuild instead of under a minute for a single-file change).

**2026-09-09 — Three bugs fixed from an end-to-end logic review.** Not
compile errors — the project already built clean — but real runtime bugs
found by tracing task/protocol timing rather than trusting "it compiles."

- **Status LED never actually blinked during an alert.** `main.cpp`'s
  display task calls `StatusLed::setAlert()` on every 200ms tick while an
  alert is active, not just once on entry. `setAlert()` used to
  unconditionally set `_blinkOn = true` and light the LED — so the very
  next line, `tick()`, immediately flipped it back off in the same
  iteration, every iteration. Net effect: the LED never stayed visibly lit.
  Fixed by making `setAlert()` a no-op once already in `Mode::Alert`
  (`status_led.cpp`), so repeated calls don't fight `tick()`'s own toggle.
  No caller changes needed.
- **BLE history backfill likely never reached a real client.** The
  firmware used to push the whole history backlog from
  `ServerCallbacks::onConnect()`, which fires at BLE link-layer connect —
  before the companion app has finished subscribing to the history
  characteristic (it was the 7th of 7 sequential characteristic
  subscriptions in `ble.js`'s `connect()`). BLE doesn't queue notifications
  for an unsubscribed characteristic; they're just dropped. Fixed by adding
  a third settings command, `0x03` = request history (no payload) — see
  "Settings wire protocol" above, now updated. The firmware only sends the
  backlog when explicitly asked via this command
  (`SettingsCallbacks::onWrite`), and `onConnect()` no longer pushes it
  automatically. `ble.js`'s `connect()` now calls the new
  `requestHistory()` method as its last step, after every characteristic
  (history included) is subscribed — verified in-browser that
  `encodeRequestHistory()` produces the single byte the firmware parser
  expects.
- **The band would deep-sleep out from under an active BLE connection.**
  `PowerMgr`'s idle timer only ever reset on a button press
  (`noteActivity()`), so with the default 15s `IDLE_TIMEOUT_MS` the band
  would drop an active, in-use BLE connection ~15 seconds after the last
  button press — including while a phone was actively reading live data.
  User's call on the fix (asked rather than guessed, since it's a
  battery-life-vs-reliability tradeoff): added a separate, longer
  `CONNECTED_IDLE_TIMEOUT_MS` (5 minutes, unmeasured starting guess) that
  `PowerMgr::update()` uses instead of `IDLE_TIMEOUT_MS` whenever
  `BleGatt::clientConnected()` is true (wired via a new `attachBle()`,
  matching the existing `attachScreen()` pattern). Deliberately still
  finite rather than "never sleep while connected" — a backgrounded or
  stuck phone connection would otherwise keep the band awake indefinitely
  and drain the battery.

Not fixed here, and not asked for at the time — remain open findings from
the same review: `BleGatt`'s `_history`/`_connectedCount` are touched from
both `bleTask` and NimBLE's own host task (via
`ServerCallbacks`/`SettingsCallbacks`) with no mutex, unlike
`sensor_data.cpp`'s correctly-guarded pattern — a real if narrow-window data
race. No sensor `begin()` return value is checked in `main.cpp`, so a
miswired sensor fails silently with no error surfaced anywhere. The
companion app's local `history` array is never cleared on
disconnect/reconnect, so it grows unboundedly across a long session. OTA
still has no trigger anywhere in the firmware (Settings-screen "Sync now"
is still static text) — unrelated to the three bugs above, not touched.

**2026-09-09 — The four remaining findings, fixed on request.**

- **`BleGatt` data race.** Added `SemaphoreHandle_t _mutex` (created in
  `begin()`), guarding every member touched from more than one task: the
  history ring buffer (`_history`/`_historyHead`/`_historyCount`),
  `_connectedCount`, and the new `_otaRequested` flag below.
  `clientConnected()` moved out of the header (it now takes the lock, so it
  can no longer be a trivial inline getter). `sendHistoryBacklog()` copies
  the buffered entries into a stack-local snapshot under the lock, then
  calls `notify()` on each *outside* the lock — holding a mutex across
  NimBLE's own radio I/O would block `pushHistory()`/`clientConnected()` on
  other tasks for no reason. `onConnect`/`onDisconnect` (NimBLE host task)
  and `pushHistory()`/`clientConnected()` (bleTask, and powerTask via
  `PowerMgr`) all now go through the same lock.
- **Unchecked `begin()` return values.** `main.cpp`'s `setup()` now
  initializes the display *first* specifically so failures have somewhere
  to be shown, then wraps every sensor/BLE/touch `begin()` call in a small
  `checkInit()` lambda that both `Serial.printf`s the failure and appends
  the name to a `failedInit` string. If anything failed, `UiScreens::
  renderBootError()` (new) shows an orange screen listing what, and
  `setup()` pauses 3 seconds before continuing — the device still boots
  either way (a hard failure to boot over a bad sensor would be worse than
  degraded readings), but now there's an on-device signal, not just a
  Serial log nobody without a debug cable would ever see. Found and fixed a
  self-inflicted bug while doing this: the first draft called
  `g_touch.begin()` twice (once unconditionally, once inside `checkInit`) —
  caught before it shipped, not after.
- **OTA now has an actual trigger.** New settings command `0x04` (see
  "Settings wire protocol" above) sets `BleGatt::_otaRequested` rather than
  blocking `onWrite()`'s own task; a new dedicated `otaTask` (see "Task
  model" above) polls `consumeOtaRequest()` once a second and calls
  `WifiSync::startOtaUpdate()` when set, logging the result. Also calls
  `g_power.noteActivity()` right before starting, so `PowerMgr`'s idle timer
  doesn't put the device to sleep mid-download — though only once, not
  throughout the transfer, so a sufficiently slow download could still in
  theory lose that race; not worth solving until there's a real OTA server
  to measure actual transfer time against. The companion app gained a
  matching "Start update" button (behind the same admin-password gate as
  Wi-Fi credentials) — see its own DEVELOPMENT.md.
- **Companion app's unbounded history array** — fixed entirely on that
  side; see its DEVELOPMENT.md.

All four verified: firmware rebuilt clean (RAM 17.0%/55,592 B, Flash
31.2%/1,043,865 B — both up from before, expected given TLS/HTTP-update
code now sits on a live path via `otaTask` rather than being dead code), and
the two new BLE commands (`0x03`... already covered, `0x04`) verified
in-browser the same way as the others: encode in JS, confirm the single
byte matches what the firmware parser expects.

**2026-09-09 — Fresh firmware-only re-analysis (build + logic trace, no new
bugs to fix, one open question finally resolved).** Rebuilt clean, then
re-traced every file touched in the last two rounds of fixes with fresh
eyes, specifically hunting for regressions the fixes themselves might have
introduced.

- **Resolved, not a bug**: previously flagged twice (in chat, not written
  down until now) as "worth verifying" — does `SettingsStore::begin()`'s
  `Preferences::begin(kNamespace, /*readOnly=*/true)` work on a genuinely
  first-ever boot, when the "keis" namespace doesn't exist yet? Checked
  against arduino-esp32's actual `Preferences.cpp` rather than continuing
  to hedge: `nvs_open()` in read-only mode on a missing namespace does fail
  (contradicts what a first, less careful search suggested — worth noting
  that AI-summarized search results disagreed with the primary source
  here, which is exactly why the second check happened), but every
  `getString()` independently guards on `Preferences`' own `_started` flag
  and falls back to the given default regardless. So first boot correctly
  produces the `config.h`/`credentials.h` defaults even though `begin()`'s
  return value is (intentionally, now documented as such in
  `settings_store.cpp`) never checked.
- **Traced the new mutex for deadlock risk**: no function that holds
  `BleGatt::_mutex` calls another function that also takes it — confirmed
  by reading every lock/unlock pair in `ble_gatt.cpp` end to end. No
  reentrant acquisition anywhere (FreeRTOS's plain `xSemaphoreCreateMutex()`
  isn't recursive, so this would deadlock instantly if it existed).
- **Confirmed `PowerMgr::_lastActivityMs` correctly does *not* need the same
  mutex treatment** as `BleGatt`'s state, despite being written from three
  tasks (`pollButtons()` on displayTask, `otaTask`, and read from
  `powerTask`). Worth writing down explicitly since it's the natural next
  question after the `BleGatt` fix: a single 32-bit-aligned scalar write
  doesn't tear on this hardware, and "note the most recent activity
  timestamp" has no correctness requirement beyond eventual consistency —
  unlike `BleGatt`'s history buffer, which is multiple fields that must
  stay consistent *as a set*.
- **Newly surfaced, not fixed**: `BleGatt::sendHistoryBacklog()`'s
  stack-allocated `HistoryEntry snapshot[kHistoryCapacity]` (600 bytes) runs
  on NimBLE's own host task when triggered by a settings write — that
  task's stack size is configured by the NimBLE-Arduino/esp-idf build, not
  by anything in this project's own `xTaskCreate` calls, so it's genuinely
  unverified whether 600 bytes plus the rest of that call's frame fits
  comfortably. Not changed (moving it off the stack would mean a
  permanently-allocated 600-byte buffer for a rarely-used path, a worse
  trade-off on a RAM-constrained target without evidence it's actually a
  problem) — flagging it for bring-up rather than guessing at a fix.
- **Re-surfaced, not new**: `applyDeviceName()` calls
  `advertising->stop()`/`start()` synchronously from inside the GATT write
  callback, while the client that just sent the write is — by definition —
  still connected. Whether restarting advertising mid-connection disrupts
  that connection on real NimBLE/real hardware is genuinely unknown from
  reading the code; add to the bring-up checklist.

Nothing here changed behavior — this pass was read-only except for the one
clarifying comment added to `settings_store.cpp`. Rebuilt clean afterward to
confirm that comment-only change didn't disturb anything.

**2026-09-11 — Button pins moved off pogo-pin-only pads.**
Cross-checked `config.h`'s pin map against the ESP32-S3 SuperMini's actual
pinout reference (espboards.dev, plus a saved copy of the same page).
Everything electrically checked out — no collision with the ESP32-S3's real
strapping pins (GPIO0/3/45/46) or the flash/PSRAM-reserved range
(GPIO26–32) — but the SuperMini's physical header only breaks out
GPIO1–13 (plus GPIO15–17, confirmed separately). GPIO0 isn't exposed at
all: the board's own pinout page notes onboard "Reset/Boot buttons," which
explains it — GPIO0 is wired internally to that button rather than left
floating.

That's a real problem for `PIN_BUTTON_1` (was GPIO21) and `PIN_BUTTON_2`
(was GPIO47): both sat on bottom-side pads with no header access, needing a
pogo-pin fixture (not available) or a hand-soldered wire directly to the
pad. Since GPIO1–13 was already almost entirely claimed by
display/touch/I2C/LIS3DH-wake wiring, only GPIO3 was free in that range —
not enough for two buttons on its own.

Reassigned:
- `PIN_BUTTON_1`: GPIO21 → **GPIO3**. Still RTC-capable (0–21), so it keeps
  working as the EXT1 deep-sleep wake source (`power_mgr.cpp`). GPIO3
  selects the JTAG interface at boot — harmless for normal operation, but
  don't hold the button down while powering on or resetting.
- `PIN_BUTTON_2`: GPIO47 → **GPIO15**, confirmed header-accessible on a
  closer read of the board's pinout reference (outside the original
  GPIO1–13 figure this doc started from). RTC-capable too, but not wired as
  a wake source — Button 1 alone was judged sufficient.

`PIN_MAX30102_INT` (GPIO40) and `PIN_MAX17048_ALRT` (GPIO38) were left
alone: neither is read anywhere in the firmware yet (declared in `config.h`
only), and there were no header pins left to move them to after the button
reassignment. GPIO40 additionally overlaps JTAG MTDO — worth reconsidering
placement if/when interrupt-driven sensor reads actually get implemented,
not before.

Updated `readme.md` §8 (both wiring diagrams, the consolidated pin-map
diagram, the §8.3 table, and a new "physical access" note explaining the
header-vs-pad distinction) and `pin-map.drawio` to match. Rebuilt clean
after the `config.h`/`power_mgr.cpp` changes.

**2026-09-13 — Dropped Button 2; touch is meant to cover its role but
doesn't yet.**
Decided to go down to one physical button, with the XPT2046 touch panel
eventually taking over Button 2's old responsibilities (Detail-screen
metric cycling, jump-to-Home) — most likely via a bottom nav bar, per
readme.md §5. This is a real, currently-unfilled gap, not a cosmetic
rename:

- `config.h`: removed `PIN_BUTTON_2` entirely (was GPIO15) rather than
  leaving it declared-but-unused — nothing in the firmware referenced it
  once `main.cpp` stopped polling it, so keeping the constant around would
  just be a dangling pin number nobody could trust meant anything.
- `main.cpp`: `pollButtons()` now only reads `PIN_BUTTON_1`. The single
  button keeps its existing Home → Detail → Settings → Home cycle
  unchanged — that loop already reaches every screen, so basic navigation
  survives losing the second button. What's actually lost: there's no
  input path left to cycle *which* metric the Detail screen shows, and no
  one-press shortcut back to Home from Settings/Alert (still reachable, just
  by cycling through instead of jumping directly).
- `ui_screens.cpp`: `UiScreens::nextDetailMetric()` is now uncalled from
  anywhere. Left it in rather than deleting it — it's the exact method
  touch navigation is meant to call once built, not genuinely dead code —
  but commented it to say so, so it doesn't read as an unexplained orphan
  to the next person in this file.
- `power_mgr.cpp`: dropped the now-stale "Button 2 is RTC-capable but not
  wired as a wake source" comment; only one button exists to discuss now.

**This means touch navigation is no longer optional polish — it's now the
only way to reach per-metric detail once built.** Before touch can actually
drive anything, still needed (tracked here since readme.md #11 is for
hardware bring-up, not this): `TouchXpt2046::readRaw()`'s raw-ADC-to-panel
calibration (currently a TODO in `touch_xpt2046.h`), and a `pollTouch()`
(or similar) wired into `displayTask` that actually calls `pressed()`/
`readRaw()` and maps a screen region to `g_ui.nextDetailMetric()` /
`g_userScreen = UiScreen::Home`. Until both exist, the Detail screen is
permanently stuck on whichever metric `UiScreens` defaults to.

Updated `readme.md` (§1 diagram's button count, §5 prose, both wiring
diagrams, the consolidated pin-map diagram, and the §8.3 table/note — GPIO15
is now called out as free rather than assigned) and `pin-map.drawio` to
match. Rebuilt clean after the `config.h`/`main.cpp`/`ui_screens.cpp`/
`power_mgr.cpp` changes.

**2026-09-13 — Touch calibration and nav bar, closing the gap the
previous entry opened.**
Implemented both pieces flagged as missing above: `TouchXpt2046`'s
raw-ADC-to-panel calibration, and a `pollTouch()` that actually drives
navigation with it.

**Calibration — one-time, two-point, self-administered:**
- `SettingsStore` gained `touchCalibrated()`/`touchCalibrationRaw()`/
  `setTouchCalibration()`, persisted in NVS via the same `Preferences`
  pattern as device name/Wi-Fi creds (new keys `tcalok`/`tcalx0`/`tcaly0`/
  `tcalx1`/`tcaly1`). Only the two raw ADC readings are stored — the
  screen-space points they correspond to are fixed by `config.h`'s new
  `TOUCH_CAL_MARGIN` (20px inset from each edge), so there's nothing
  per-unit to persist there.
- `TouchXpt2046` gained `setCalibration()` (takes both calibration points'
  raw+screen pairs) and `readCalibrated()` (applies the resulting linear
  map, per axis, to a live reading). Two diagonal points is sufficient for
  a resistive panel — it's linear, not the multi-point fit a capacitive
  panel's distortion would need.
- `main.cpp` gained `applyStoredOrNewCalibration()`, called once from
  `setup()` after Touch initializes (only if it initialized OK — see
  below), before any task is created: if `SettingsStore` already has a
  calibration, apply it immediately; otherwise draw a crosshair at each of
  the two calibration points in turn (`UiScreens::renderCalibrationPrompt()`,
  a one-shot boot-time screen alongside the existing `renderBootError()`)
  and block until each is touched, then persist and apply.
- **Real failure mode considered, not just the happy path**: a touch panel
  that's miswired, unpowered, or genuinely broken would make the "wait for
  a touch" loop block forever, hanging boot completely — on a device with
  no serial cable attached, that's indistinguishable from a bricked unit.
  `waitForTouchOrSkip()` polls the physical button alongside the touch
  panel and bails out (leaving calibration unset, tried again next boot)
  if the button wins the race. `applyStoredOrNewCalibration()` is also
  only called when `g_touch.begin()` itself succeeded — no point running a
  calibration wait loop against a controller that already failed to
  initialize.
- **What's genuinely unresolved without real hardware**: whether the touch
  overlay's raw X/Y axes are rotated relative to the display's drawn
  coordinates is a physical-mounting fact, not something the calibration
  math can determine — added `TOUCH_SWAP_XY` (`config.h`, default false)
  as the escape hatch, documented as bring-up-time, not computed.

**Navigation — bottom nav bar plus Detail-metric cycling:**
- `config.h` gained `NAV_BAR_HEIGHT` (18px), reserved at the bottom of
  every screen except the full-screen Alert.
- `UiScreens::drawNavBar()` draws three equal-width zones (Home/Detail/Set)
  with the active one highlighted, called from `renderHome()`,
  `renderDetail()`, and `renderSettings()`. `navZoneAt()` is a `static`
  method (no instance state needed) doing the matching hit-test, kept in
  `ui_screens.cpp` specifically so the tap zones can never drift out of
  sync with what `drawNavBar()` actually drew — main.cpp calls it rather
  than recomputing zone boundaries itself.
- `UiScreens::renderHome()`'s battery bar/text were nudged up (barY 130→118,
  text y 146→132) — the old layout drew all the way to y≈154 on a 160px
  panel, which would have collided with the new 18px nav bar. Detail and
  Settings already left enough clearance and needed no layout change.
- `main.cpp`'s new `pollTouch()` mirrors `pollButtons()`'s shape exactly
  (edge-triggered on the rising press, calls `g_power.noteActivity()`,
  polled once per 200ms display-task tick): a tap landing in the nav bar's
  Y band jumps to `UiScreens::navZoneAt(x)`; a tap above it, while on
  Detail, calls the previously-orphaned `nextDetailMetric()`. Naturally a
  no-op before calibration exists, since `readCalibrated()` returns false
  until `setCalibration()` has been called — no extra guard needed in
  `displayTask()`.

Updated `readme.md` §5 (button/touch division of labor) and §10's status
line to match. Rebuilt clean after all of the above.

**2026-09-13 — On-device Settings screen: brightness + sync-now, alert
limits deliberately excluded.**
Scoped down from "make Settings interactive" to exactly two controls,
decided before writing any code (see the recommendation given in chat):
brightness cycling and a "sync now" trigger, both using the same
tap-zone mechanism `pollTouch()` already had for Detail-metric cycling.
Alert limits stay phone-app-only — 5 numeric thresholds need real number
entry to edit safely, which a tap-to-cycle UI on a 128x160 screen with no
keyboard can't offer without real risk of a stray tap silently changing a
safety threshold; the companion app already does this properly. The
Settings screen now says so explicitly ("Alert limits: phone app only")
rather than a `TODO` that looked like unfinished work.

**Brightness needed real PWM first — it was never actually implemented.**
`Screen::setBacklight()` was a `digitalWrite()` on/off placeholder (its own
TODO comment said as much). Switched it to `ledc`: `ledcSetup()` +
`ledcAttachPin()` + `ledcWrite()`, the channel-based API — checked
`esp32-hal-ledc.h` in the actually-installed arduino-esp32 core (3.20017)
directly rather than assuming, since the newer pin-based `ledcAttach(pin,
freq, res)` signature exists in some core versions but not this one.
`PowerMgr`'s existing `setBacklight(0)` sleep call needed no change — 0%
duty is still fully off either way.

Three discrete levels (`config.h`: `BRIGHTNESS_PERCENTS` = {30, 65, 100},
`BRIGHTNESS_LABELS` = {"Low", "Med", "High"}) rather than a continuous
slider — a tap only cycles forward, so a slider's precision would be
wasted and slower to reach the far end. `SettingsStore` persists the
*index*, not the raw percent, so the three levels can be retuned later
without stale NVS data pointing at the wrong percent.

**"Sync now" turned out to already be OTA, not a new concept** —
`WifiSync::startOtaUpdate()`'s own doc comment already said "e.g. in
response to the Settings screen's 'sync now' action," and readme.md #5
has described Settings' sync action as "briefly turns on Wi-Fi" since the
original scaffold. Rather than inventing a parallel request mechanism,
made `BleGatt::requestOta()` public (was private, called only from the
BLE settings-write handler) so the Settings-screen tap can set the exact
same `_otaRequested` flag `otaTask` already polls via
`consumeOtaRequest()`. Renamed its log line from `"[BLE] OTA update
requested"` to `"[OTA] requested"` since it no longer only fires from BLE.

**Wiring**, all in `main.cpp`:
- `applyBrightnessLevel(level, persist)` is the one place that touches all
  three: `Screen::setBacklight()` (actual PWM), `UiScreens::setBrightnessLevel()`
  (what the Settings screen displays), and — only when `persist` is true —
  `SettingsStore::setBrightnessLevel()` (NVS). `persist=false` is for
  applying the already-stored level once at boot, where writing it
  straight back to NVS would just be a pointless flash write for no
  change.
- `pollTouch()` now branches on `g_userScreen` when a tap lands above the
  nav bar: Detail cycles the metric (as before), Settings looks up
  `UiScreens::settingsZoneAt(y)` (a new static hit-test, same
  never-drift-out-of-sync reasoning as `navZoneAt()`) and either cycles
  brightness or calls `g_ble.requestOta()`.
- `UiScreens::renderSettings()` draws real state instead of `TODO` text,
  and gained a matching `settingsZoneAt()` — the row Y-bands
  (`kSettingsBrightnessRowY`/`kSettingsSyncRowY`/`kSettingsAlertRowY`,
  `ui_screens.cpp`) are defined once and read by both.

Rebuilt clean after all of the above.

**2026-09-13 — Real step-counting and fall-detection heuristics,
replacing the unimplemented stubs.**
`MotionLis3dh::update()` previously only computed `isMoving()` — `_stepCount`
stayed 0 forever and `_fallDetected` stayed false forever, both declared
but never written anywhere (not merely untuned, genuinely absent). Added
two independent, simple, well-established heuristics — not gait analysis
or ML, both explicitly documented as placeholder-tuned:

- **Step counting**: rising-edge counter on acceleration magnitude —
  count once when magnitude crosses above `kStepThresholdG` (1.2g) from
  below, debounced by `kStepDebounceMs` (250ms, capping counting at 240
  steps/min) so one physical step's single rise-then-fall isn't counted
  twice.
- **Fall detection**: the standard two-stage free-fall-then-impact
  heuristic — arm a candidate when magnitude drops below
  `kFreeFallThresholdG` (0.4g, near-weightless), confirm it if magnitude
  spikes above `kImpactThresholdG` (2.5g) within `kFallWindowMs` (1000ms)
  of the *most recent* low reading (re-armed on every qualifying sample,
  not just the first, so the window tracks "impact soon after motion
  stops indicating free-fall" rather than a stale first-touch timestamp).
- **A real design gap, not an oversight, is now handled explicitly**:
  once `_fallDetected` latches true, nothing else in the firmware ever
  clears it — no button/touch "acknowledge" exists for this alert
  specifically (unlike HR/SpO2/temp alerts, which clear naturally once
  the reading is back in range). Without a fix, one fall event would
  permanently occupy the Alert screen until reboot. Added
  `kFallAlertDurationMs` (10s) — the flag auto-clears that long after being
  set. Not a validated duration, just "long enough to notice, not
  forever."

All five constants are `private static constexpr` inside `MotionLis3dh`,
matching where the pre-existing `kMovementThresholdG` already lived —
kept consistent with that existing local pattern rather than moving
tunables to `config.h`, since this file already established the
per-sensor-class placement.

Added an explicit `#include <Arduino.h>` to `motion_lis3dh.cpp` for
`millis()` rather than relying on it arriving transitively via
`Wire.h`/`Adafruit_LIS3DH.h` — same category of mistake as the
already-fixed missing `<Arduino.h>` in `ble_gatt.cpp` earlier this
project, worth avoiding proactively rather than waiting for the build to
catch it again.

Updated readme.md's status line (§10) to describe these as real
heuristics needing tuning, not unimplemented stubs. Rebuilt clean.

**Same-day follow-up: caught a real range-clipping bug on re-check.**
`begin()` was still configuring the sensor for the library's `LIS3DH_RANGE_2_G`
default — set before fall detection existed, back when the largest
threshold in play was `kStepThresholdG` at 1.2g. `kImpactThresholdG` (2.5g)
exceeds that ±2g ceiling: a real fall's impact spike would clip at 2g
before the code could ever read a value crossing 2.5g, so fall detection
as written could never actually fire, regardless of how well-tuned the
threshold itself was. Missed this when the heuristic was first written;
caught it on a deliberate re-check rather than by a user report or a
build failure — the compiler has no way to catch a sensor range/threshold
mismatch like this, it's a logic error, not a type error.

Fixed by switching to `LIS3DH_RANGE_8_G`. Checked this doesn't cost
`kMovementThresholdG`/`kFreeFallThresholdG` meaningful precision — the
LIS3DH's 12-bit high-res output still resolves well under 0.1g/step at
±8g, comfortably fine-grained relative to those two thresholds (0.15g and
0.4g). Rebuilt clean.

**2026-09-13 — Fixed the two remaining bring-up-checklist risks from the
BLE mutex/history-race review, rather than leaving them as "verify on
hardware."**

**BLE advertising restart mid-connection.** `applyDeviceName()` used to
call `advertising->stop()`/`start()` synchronously, inside the GATT write
callback that requested the rename — the exact callback NimBLE's host
stack sends its automatic ATT Write Response for the moment it returns,
on that same connection. Restarting advertising there risked racing that
in-flight response. There's no in-callback hook for "after the response
is sent" — it's the stack's own bookkeeping, invisible to application
code — so the fix defers the whole thing to a later task tick instead of
trying to reorder anything inside the callback:

- `applyDeviceName()` now only persists the name (`SettingsStore`) and
  queues it (`_pendingDeviceName`/`_pendingNameChange`, guarded by the
  existing mutex) — same shape as the pre-existing `_otaRequested`
  pattern.
- New `BleGatt::pollPendingNameChange()` does the actual
  `NimBLEDevice::setDeviceName()` + advertising restart, test-and-clearing
  the pending flag. Polled from `bleTask` (`main.cpp`) once per second,
  right after `notify()` — a different FreeRTOS task than NimBLE's host
  task, so by the time it runs, the write callback (and thus the
  response) has necessarily already completed.

**NimBLE host task stack headroom.** `sendHistoryBacklog()`'s 600-byte
`HistoryEntry snapshot[kHistoryCapacity]` was a stack-local array on
NimBLE's own host task — moved to a new class member, `_historySnapshot`
(`ble_gatt.h`). Trades an unverified transient stack cost for a permanent
600-byte RAM reservation (on top of the existing 600-byte `_history[]`
buffer) — negligible on this target's headroom (17% RAM used so far), but
worth being honest that it's a genuine tradeoff, not a free fix: a
static/member buffer isn't inherently reentrant the way a stack-local one
is. It's only safe here because `sendHistoryBacklog()` can never run twice
concurrently (NimBLE's single host task processes GATT callbacks
serially) — if a second call path to it is ever added, this buffer needs
its own guard.

**Honest about what this doesn't fix**: moving the one known 600-byte
contributor off the stack removes the largest *identified* risk, not
every possible one — NimBLE's own internal ATT/GATT processing and any
`String`/`Serial.printf` stack usage elsewhere in the same call chain are
still unmeasured. Real confirmation still needs `uxTaskGetStackHighWaterMark()`
on real hardware after triggering a history request. Updated readme.md
§11 items 6 and 7 to reflect "fixed"/"reduced" rather than "unverified."
Rebuilt clean.

**2026-09-13 — End-to-end verification pass (companion app driven live in
a real browser; firmware re-traced logic-only, still no physical
hardware). One real gap found and fixed.**

The companion app was served locally and driven through an actual
browser session rather than just read: connect flow (real
`requestDevice()` chooser, confirmed it genuinely hits `NotFoundError`
with no adapter present and recovers cleanly), theme toggle (confirmed
an actual `getComputedStyle` repaint, not just a stored preference),
the admin-password gate on Wi-Fi save/OTA-start (cancel, mismatch,
first-set, session-persistence across both buttons, wrong-password and
correct-password on a later session after a reload), and the
disconnected-write error path. Also called the pure `decoders.js`/
`settings-protocol.js` functions directly with synthetic byte buffers
to confirm the wire format matches `ble_gatt.cpp` exactly, including
the IEEE-11073 float and the settings-command length caps — all
round-tripped correctly, no drift between the two sides.

One environment artifact, not an app bug: `service-worker.js`
registration fails in this sandboxed browser pane even for a
trivial one-line control script with no app code in it, while a plain
`fetch()` of the same file succeeds — confirmed environment-only by
swapping in the minimal script and reproducing the identical failure.
Not fixed (nothing to fix); re-verify PWA installability on a real
device/Chrome before relying on it.

**Real gap found and fixed: touch could blindly trigger Settings
actions while the Alert screen was covering them.** `main.cpp`'s
`pollTouch()` had no awareness of whether an alert was currently
overriding the display. `renderAlert()` draws no nav bar and no
Settings rows, but `pollTouch()` still ran its normal nav-bar/
Settings-zone hit-test against whatever `g_userScreen` last was — so a
tap landing where the Brightness or Sync-Now row would normally be
could silently cycle backlight brightness or fire a real
`g_ble.requestOta()` network request, with no visual feedback that
anything happened, precisely while the wearer is looking at a
full-screen fall/vitals alert they can't otherwise interact with.

Checked first whether this removed any real capability: it doesn't.
Alerts have never been manually dismissible — `sensorDataIsAlert()` is
re-evaluated fresh every display-tick from live sensor state (HR/SpO2/
temp/battery-low alerts clear themselves the instant the reading is
back in range), and fall detection clears itself via
`kFallAlertDurationMs` (`motion_lis3dh.cpp`) regardless of button or
touch input. `pollButtons()`'s `case UiScreen::Alert:` has always been
dead code — `g_userScreen` is never actually set to `Alert`, only
`Home`/`Detail`/`Settings`.

Fix: `pollTouch()` now takes an `alertActive` parameter (computed once
in `displayTask()`, before `pollButtons()`/`pollTouch()` run, and
passed to the latter) and returns immediately after
`g_power.noteActivity()` — so a tap during an alert still counts as
activity for the idle/sleep timer, but no longer reaches the nav-bar
jump or the Settings-zone dispatch. Button navigation was deliberately
left alone: cycling `g_userScreen` only changes *where the user lands
once the alert clears*, not an immediate side effect the way touch's
Settings zone can trigger one, and the existing `g_userScreen` design
comment already documents that navigation state should keep working
during an alert.

Rebuilt clean after the change.

**2026-09-13 — First real hardware flash: fixed a flash-size/partition
mismatch causing an immediate boot-loop crash.**

First time this firmware ran on the actual ESP32-S3 SuperMini unit
(everything before this was build-verified and logic-traced only, no
physical hardware existed). It crash-looped every boot, ~240ms in,
before `setup()` got meaningfully far:

```
E (210) spi_flash: Detected size(4096k) smaller than the size in the
binary image header(8192k). Probe failed.
assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)
```

(preceded by `E (240) esp_core_dump_flash: Core dump flash config is
corrupted! CRC=... instead of 0x0` — secondary noise: the panic
handler's own attempt to persist crash details to the coredump
partition, which also can't succeed once flash init itself has
failed. A full chip erase (`pio run --target erase`) was tried first
and did *not* fix it, correctly ruling out stale-partition-data as the
cause and pointing at something structural.)

**Root cause, confirmed against the actually-installed toolchain, not
guessed:** `boards/esp32-s3-devkitc-1.json` in the installed
`espressif32` platform declares `upload.flash_size = "8MB"` and
`upload.partitions = "default_8MB.csv"` — generic devkit-board
defaults, not a fact about this project's hardware. `platformio.ini`
never overrode either, so the build silently inherited them. The real
SuperMini unit has 4MB flash. Confirmed the connection precisely:
`default_8MB.csv`'s `app0` partition is exactly `0x330000` =
**3,342,336 bytes** — the identical "Flash total" figure every build
this whole project has reported (e.g. "31.5% used, 3342336 bytes
total"), meaning this mismatch has been latent since the very first
scaffold and only surfaced now because this was the first time the
binary actually ran on the real chip rather than just being sized and
linked against it.

Fixed in `platformio.ini`:
```
board_upload.flash_size = 4MB
board_build.partitions = default.csv
```
`default.csv` (the framework's stock 4MB table — confirmed by reading
it directly from the installed `framework-arduinoespressif32` package,
not assumed) keeps OTA capability (`ota_0`/`ota_1` app slots, unlike
`huge_app.csv`'s single-app layout, which `WifiSync`'s
`httpUpdate.update()` needs) plus `spiffs`/`coredump`, all within a
real 4MB budget: `nvs` 20K, `otadata` 8K, `app0`/`app1` 1.25MB each,
`spiffs` 1.375MB, `coredump` 64K.

**Real consequence, not just a build-flag fix: available flash margin
dropped a lot.** Same 1,053,789-byte image, now measured against the
real 1,310,720-byte (1.25MB) OTA app slot instead of the previous
phantom 3.19MB one — usage went from a comfortable 31.5% to **80.4%**,
leaving roughly 257KB of headroom. Worth watching on future additions;
if the app ever needs to grow past what `default.csv`'s 1.25MB OTA
slots allow, the tradeoff is a custom partition table (e.g. dropping
`spiffs`, which nothing in this project currently uses, to grow the
app slots) rather than reverting to the 8MB assumption, since the
physical chip genuinely is 4MB.

Rebuilt clean after the fix (RAM unchanged at 56,328 bytes/17.2%;
Flash 1,053,789/1,310,720 bytes, 80.4%). Not yet re-verified against
actual hardware as of this entry — next step is reflashing and
confirming the boot-loop is actually gone, not just that the build
numbers now make sense.

**Same day, second hardware crash after the flash-size fix — a genuine
TFT_eSPI/core version mismatch, not stale flash.** Reflashing after
the flash-size fix got past the `spi_flash` assert and into `setup()`
(NVS's expected first-boot "not found" log, then I2C init), but hit a
new, different crash immediately after: `Guru Meditation Error: Core 1
panic'ed (StoreProhibited)`, `EXCVADDR: 0x00000010`.

**Diagnosed precisely, not from the backtrace alone** — symbolized the
crash addresses against the actual built ELF
(`xtensa-esp32s3-elf-addr2line`) and disassembled the faulting
instruction (`xtensa-esp32s3-elf-objdump`), which is how this got
pinned down exactly rather than guessed at:

```
TFT_eSPI::begin_tft_write() TFT_eSPI.cpp:81
 (inlined by) TFT_eSPI::writecommand() TFT_eSPI.cpp:982
 <- TFT_eSPI::init() TFT_eSPI.cpp:692
 <- Screen::begin() src/display/screen.cpp:13
 <- setup() src/main.cpp:329
```

The faulting instruction is the `SET_BUS_WRITE_MODE` macro expansion
(`*_spi_user = SPI_USR_MOSI | SPI_CK_OUT_EDGE`,
`Processors/TFT_eSPI_ESP32_S3.h`) — a direct hardware-register write
TFT_eSPI does for speed, bypassing the normal `SPIClass` API. Traced
the address itself through the macro chain (checked against the
actually-installed headers, not assumed): `_spi_user` expands to
`SPI_USER_REG(SPI_PORT)`, `SPI_USER_REG(i)` is `REG_SPI_BASE(i) +
0x10` (`soc/spi_reg.h`), and `REG_SPI_BASE(i)` is
`((i)>=2) ? (DR_REG_SPI2_BASE + (i-2)*0x1000) : (0)` (`soc/soc.h`) —
i.e. it deliberately returns 0 for `i` below 2, since indices 0/1 are
the chip's internal flash/PSRAM SPI buses, not general-purpose ones.
`SPI_PORT` is `#define`d to the bare `FSPI` macro for
`CONFIG_IDF_TARGET_ESP32S3` in TFT_eSPI's own
`Processors/TFT_eSPI_ESP32_S3.h`, and the currently-installed
Arduino-ESP32 core defines `FSPI = 0` for S3-family chips
(`esp32-hal-spi.h`) — a "logical SPI slot" numbering, not the raw
peripheral index TFT_eSPI's macro assumed. So
`SPI_USER_REG(0) = REG_SPI_BASE(0) + 0x10 = 0 + 0x10 = 0x10` — the
exact crash address, confirmed by literally reading it back out of
the disassembly (`movi.n a2, 16` computed as a compile-time constant,
not a runtime value).

This is a known TFT_eSPI/core version-numbering mismatch for
ESP32-S3, and the library ships its own escape hatch for it:
`USE_FSPI_PORT`, which forces `SPI_PORT` to the literal correct value
(`2`) instead of deriving it from the ambiguous `FSPI` macro. Added
`-D USE_FSPI_PORT=1` to `platformio.ini`'s `build_flags`. Checked the
side effect before accepting it: with this flag, TFT_eSPI's
`Processors/TFT_eSPI_ESP32_S3.c` also switches its internal `spi`
handle from `SPIClass& spi = SPI;` (aliasing the same global object
`main.cpp` calls `SPI.begin()` on) to its own separate
`SPIClass spi = SPIClass(FSPI);` instance — still the same physical
GPSPI2/FSPI peripheral (the global `SPI` object is itself constructed
with `FSPI` by default on this core, confirmed in the installed
`SPI.cpp`), just a second C++ handle onto it, with its own lock and
attach state. `TFT_eSPI::init()`'s own `spi.begin(TFT_SCLK, TFT_MISO,
TFT_MOSI, -1)` call already passes the project's real SCLK/MOSI pins
explicitly (not relying on board-default pins), so this doesn't
misroute anything. Not a live concurrency risk either: `pollTouch()`
(the other SPI user on this bus, via `TouchXpt2046`, which still goes
through the shared global `SPI` object) and all display rendering run
serially within the same `displayTask`, never concurrently from
another task — so the two SPIClass instances not sharing a lock
doesn't matter in this codebase's actual usage pattern. Worth
re-checking if SPI is ever touched from a second task in the future.

Rebuilt clean (RAM 56,360/327,680 bytes — 17.2%, +32 bytes; Flash
1,058,529/1,310,720 bytes — 80.8%, +4,740 bytes over the flash-size
fix's build, consistent with TFT_eSPI now owning a second SPIClass
instance). Not yet re-verified on hardware as of this entry — this is
the second of what may be more first-bring-up issues; each one only
becomes visible by actually running on the real chip; see readme.md
§11 if this becomes a pattern worth its own checklist item.

**Same day, third hardware issue: the shared I2C bus was silently
running at 400kHz because of PPG's `begin()`, breaking MLX90614.**
With the SPI crash fixed, `setup()` got through display/BLE init
cleanly, but three of four I2C sensors reported `[INIT] X failed to
initialize`. Rather than guess address-by-address, added a temporary
boot-time I2C bus scan (`main.cpp`, right after `Wire.begin()`, marked
for removal once bring-up is done) — `Wire.beginTransmission(addr)` /
`endTransmission()==0` across `0x01`-`0x7E`, printing every address
that ACKs.

First real finding from the scan, not a guess: `0x5A` (MLX90614,
config.h's `I2C_ADDR_MLX90614`) **was present and responding** — yet
`TempMlx90614::begin()` still failed. Since the scan runs before any
sensor's own `begin()`, and the underlying check both use is identical
(`Adafruit_I2CDevice::detected()` does the exact same
`beginTransmission`/`endTransmission` probe our scanner does —
confirmed by reading BusIO's source, not assumed), something between
the scan and `Temp::begin()` was changing the bus out from under it.
Traced it to `PpgMax30102::begin()` (`ppg_max30102.cpp`), which runs
immediately before Temp's `checkInit()` and calls
`_sensor.begin(bus, I2C_SPEED_FAST)` — SparkFun's `MAX30105::begin()`
calls `_i2cPort->setClock(i2cSpeed)` (confirmed in the installed
library source), which reconfigures the *shared* `Wire` bus's clock
for every sensor after it, not just PPG's own transactions, and
nothing ever set it back. `I2C_SPEED_FAST` is 400kHz; the MLX90614 is
well known for not reliably tolerating I2C speeds above its 100kHz
standard-mode spec. That's the whole bug: the scan (100kHz, before PPG
ran) found the MLX90614 fine; Temp's own `begin()` (400kHz, after PPG
ran) failed the identical check against the identical, still-present
device.

Fixed by changing `ppg_max30102.cpp` to `I2C_SPEED_STANDARD` (100kHz)
— also the SparkFun library's own default, so this wasn't a
deliberate-but-risky choice being reversed, just an unnecessary
opt-into-Fast-Mode nobody had a reason for. Multiple unrelated sensors
sharing one bus means the slowest one's spec governs for all of them;
nothing on this project's sensor list needs 400kHz badly enough to
justify the risk. Rebuilt clean (RAM/Flash unchanged from the prior
entry's numbers, as expected for a one-constant change).

The scan's second finding is still open: it found `0x1D` where Motion
(LIS3DH, expected `0x18` or `0x19` depending on the `SA0` strap) should
be — `0x1D` isn't a value the LIS3DH's `SA0` pin can ever produce, so
this isn't just a strapping surprise, it points at a possibly
different physical part than expected (ADXL345/ADXL343 and MMA8452Q
both commonly use `0x1D`). Not fixed yet — confirming the actual
breakout/module in use before touching `config.h`'s address, since the
`Adafruit_LIS3DH` driver also checks a chip-ID register on begin() and
a different chip's register map wouldn't work even at the right
address. Fuel gauge (`0x36`) not found in the scan is expected —
confirmed not physically connected yet, not a bug.

**Same day, fourth hardware issue: the Motion sensor is a genuinely
different chip, the LIS3DSH, not the LIS3DH this board was speced
around.** The user confirmed the physical module by its silkscreen —
not a strapping quirk. Verified this isn't just an address difference
before touching any code: LIS3DSH and LIS3DH are different ST parts
with different register maps and different `WHO_AM_I` values (LIS3DSH
= `0x3F`, confirmed against ST's own `stm32-lis3dsh` reference driver
source on GitHub, not the datasheet's prose alone; LIS3DH = `0x33`) —
so `Adafruit_LIS3DH::begin()` would keep failing even pointed at
`0x1D`, since its own `WHO_AM_I` check would mismatch regardless of
address.

Checked what a fix actually required before writing anything: the one
community Arduino library for this chip (`yazug/LIS3DSH`, 5 commits)
looked thin and exposed no click/tap-detection API from its header, so
it would silently drop the double-tap deep-sleep wake feature the
project already has for LIS3DH. Given the size of the decision (new
driver, re-verified units, a real feature gap either way), asked the
user rather than picking silently — chose to build accel/steps/fall
now and explicitly defer double-tap wake, either path.

**Built a direct-register I2C driver instead of adopting the thin
library**, matching this project's established practice of reading
authoritative sources rather than trusting a lightly-maintained
third-party one. Register addresses, bit layouts, and the mg/digit
sensitivity table were all taken from STMicroelectronics' own
`stm32-lis3dsh` reference driver (`lis3dsh.h`/`lis3dsh.c`), fetched
directly rather than assumed:

- `WHO_AM_I` (`0x0F`, expect `0x3F`), `CTRL_REG4` (`0x20`: ODR[7:4] |
  BDU[3] | ZEN[2] | YEN[1] | XEN[0]), `CTRL_REG5` (`0x24`:
  FSCALE[5:3]), and `OUT_X_L` through `OUT_Z_H` (`0x28`-`0x2D`, six
  consecutive bytes, little-endian per axis) — confirmed byte-for-byte
  against the fetched header, not paraphrased from memory.
- Configured for 100 Hz, block-data-update, XYZ enabled, ±8g full
  scale — the same headroom reasoning as the original LIS3DH tuning
  (`kImpactThresholdG` is 2.5g; the range ceiling needs room above it).
  Sensitivity at ±8g is `0.24 mg/digit` per ST's own table, applied
  after a `>>4` (the 16-bit output register holds a 12-bit reading,
  left-justified — ST's own driver does the same shift before scaling).
- Renamed the file/class throughout (`motion_lis3dh.*` →
  `motion_lis3dsh.*`, `MotionLis3dh` → `MotionLis3dsh`,
  `I2C_ADDR_LIS3DH`/`PIN_LIS3DH_INT1` → the `LIS3DSH` equivalents) —
  the old names were now actively misleading about what chip this
  actually is, not just stale. Updated every call site: `main.cpp`
  (include, instance, the `configureDoubleTapWake()` call site removed
  with a comment explaining the deferral rather than left calling a
  method that no longer exists), `power_mgr.cpp` (pin rename, and its
  wake-source comment now says plainly that the LIS3DSH side of the
  EXT1 wake mask is registered but inert until double-tap detection
  is implemented — a harmless no-op, not a silent gap). Removed
  `adafruit/Adafruit LIS3DH` and `adafruit/Adafruit Unified Sensor`
  from `platformio.ini`'s `lib_deps` — confirmed neither
  `Adafruit MLX90614 Library` nor `Adafruit MAX1704X` depend on
  Unified Sensor (both only declare `Adafruit BusIO`), and nothing in
  `src/` includes `Adafruit_Sensor.h` directly, so it was purely an
  LIS3DH transitive dependency with nothing left pulling it in.
- All of `MotionLis3dsh`'s step-counting and fall-detection logic is
  copied verbatim from the old `MotionLis3dh` — those thresholds are
  "g" units, not chip-specific, and the underlying algorithm doesn't
  care which sensor produced the magnitude reading.

**Double-tap wake is explicitly not implemented** — `MotionLis3dsh`
has no `configureDoubleTapWake()` method at all (not a stub that
silently does nothing). The LIS3DSH does support click/double-click
detection in silicon (`CLICK_CFG` register, confirmed via the same ST
source), so this is a real, addressable gap when it's worth doing, not
a hardware limitation — see readme.md #11 item 5 for the tracked
status.

Updated `readme.md` throughout (§1, §4's sensor table, §5's wake
section and state diagram, §6, §8's two wiring diagrams and the pin
map/I2C tables, §9's dependency list, §10's status line, and §11 with
two new checklist items covering the TFT_eSPI `SPI_PORT` fix from
earlier today and this I2C/chip-identification fix) rather than
leaving it describing a library and a chip this firmware no longer
uses. Rebuilt clean; not yet reflashed/re-verified on hardware as of
this entry — that's the next step, along with confirming the I2C
speed fix (`ppg_max30102.cpp`, same day, above) actually gets Temp
initializing now too.

**Same day, fifth hardware issue: reflashed both fixes above — all
three real sensors (PPG, Temp, Motion) confirmed initializing clean.**
BLE also connected and subscribed successfully. But the companion app
still showed only `--`. Added a temporary diagnostic
(`Serial.printf` at the top of `BleGatt::notify()`, both the
early-return and the normal path — readme.md #11, remove once real
values are confirmed reaching the app) to see exactly what the
firmware believed on each tick. It never printed at all, not even
before the phone connected — which is itself the finding, since
`bleTask` should tick once a second from very early in boot regardless
of any BLE client.

**Root cause: the physical display/touch panel isn't connected yet**
(bring-up is proceeding sensor-first), and `setup()`'s call order means
`g_ble.begin()` — which starts BLE advertising and brings the whole
GATT server up — runs *before* the blocking touch-calibration step.
NimBLE's own internal host task handles connections independently of
any of this project's own FreeRTOS tasks, so the phone could connect
and subscribe successfully even while `setup()` itself was still stuck
waiting inside `applyStoredOrNewCalibration()` — meaning `bleTask`,
`sensorTask`, and every other task hadn't been created yet at all. Not
a bug in the BLE path; a real gap in `waitForTouchOrSkip()`, whose own
existing comment already claimed "a broken or unwired touch panel must
not be able to hang boot forever" — true for a *broken* panel (the
button still escapes that), but not for a **not-yet-connected** one
with nobody standing by to press the button during unattended bring-up.

Fixed by adding a 10-second timeout to `waitForTouchOrSkip()`
(`main.cpp`) alongside the existing touch/button race — logs
`[INIT] Touch calibration timed out, skipping` and returns false (same
as the button path) rather than blocking indefinitely. This is the
real fix for the function's own stated guarantee, not a new one.
Confirmed via earlier logs that `TouchXpt2046::begin()` (the
XPT2046_Touchscreen library) returns true regardless of whether a
panel is actually wired — it never fails init from a check-worthy
error, so `touchOk` has been true throughout this bring-up and the
calibration wait was always going to run.

Once this reaches `setup()`'s task-creation block, `bleTask` starts
ticking and the `[BLE DEBUG]` diagnostic added above should start
printing real sensor values once a second — which directly doubles as
what was asked for (seeing sensor readings over serial without the
display connected), no separate serial-dump feature needed. Rebuilt
clean; not yet reflashed/re-verified on hardware as of this entry.
