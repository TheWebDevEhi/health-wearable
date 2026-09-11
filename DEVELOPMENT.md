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
