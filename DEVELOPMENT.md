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
  (LIS3DH INT1 on GPIO1, Button 1 on GPIO21) wired for real since they're
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
