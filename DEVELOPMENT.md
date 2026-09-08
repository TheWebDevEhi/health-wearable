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
