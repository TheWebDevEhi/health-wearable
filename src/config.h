#pragma once

#include <cstdint>

// Bump alongside the "Version:" line at the top of readme.md.
#define FIRMWARE_VERSION "0.1.0"

// Default BLE device name, used only until SettingsStore has something
// persisted (readme.md #7) — the companion app can rename it at runtime.
constexpr const char *DEFAULT_DEVICE_NAME = "Keis Band";

// ---------------------------------------------------------------------------
// Pin map — mirrors readme.md #8.3. This is the single source of truth for
// GPIO numbers; do not hardcode pin numbers anywhere else in the firmware.
// ---------------------------------------------------------------------------

// Shared SPI bus (display + touch)
constexpr int PIN_SPI_SCK  = 12;
constexpr int PIN_SPI_MOSI = 11;
constexpr int PIN_SPI_MISO = 13;

// Display (ST7735) — also set in platformio.ini build_flags for TFT_eSPI
constexpr int PIN_TFT_CS  = 10;
constexpr int PIN_TFT_DC  = 4;
constexpr int PIN_TFT_RST = 5;
constexpr int PIN_TFT_BL  = 6;  // PWM backlight

// Touch (XPT2046)
constexpr int PIN_TOUCH_CS  = 7;
constexpr int PIN_TOUCH_IRQ = 2;

// Shared I2C bus (all four sensors)
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;

// Motion wake — RTC-capable, required for deep-sleep wake (readme.md #5).
// Registered as an EXT1 wake source (power/power_mgr.cpp) but currently
// inert: double-tap detection isn't configured on the LIS3DSH yet
// (deferred — see src/sensors/motion_lis3dsh.h), so this pin never
// actually asserts. The button remains the only live wake source for now.
constexpr int PIN_LIS3DSH_INT1 = 1;

// Button (to GND, internal pull-up).
// Down to one physical button — the second was dropped in favor of the
// touch panel eventually covering that navigation (readme.md #5); until
// touch navigation is actually built, Detail-screen metric cycling has no
// input path (see DEVELOPMENT.md). Moved off the ESP32-S3 SuperMini's
// non-header pads (readme.md #8.3 "Pins to keep clear") onto a pin actually
// reachable without pogo pins. GPIO3 is a strapping-adjacent pin per the
// board's pinout reference, not hazard-free — see the note in readme.md
// #8.3. Don't hold the button down while powering on or resetting the
// board: GPIO3 selects the JTAG interface at boot.
constexpr int PIN_BUTTON_1 = 3;   // RTC-capable; doubles as a wake source

// Status LED (on-board WS2812)
constexpr int PIN_STATUS_LED = 48;

// Optional interrupt lines
constexpr int PIN_MAX30102_INT  = 40;
constexpr int PIN_MAX17048_ALRT = 38;

// ---------------------------------------------------------------------------
// I2C addresses — mirrors readme.md #8.4.
// ---------------------------------------------------------------------------
// The board was originally speced around the LIS3DH (0x18/0x19 depending on
// SDO strap); the physical unit is actually an LIS3DSH — a different chip,
// not just a different address (see src/sensors/motion_lis3dsh.h). 0x1D is
// hardware-confirmed via a boot-time I2C scan (readme.md #11), not assumed
// from the datasheet's SA0 table.
constexpr uint8_t I2C_ADDR_LIS3DSH  = 0x1D;
constexpr uint8_t I2C_ADDR_MLX90614 = 0x5A;
constexpr uint8_t I2C_ADDR_MAX17048 = 0x36;
constexpr uint8_t I2C_ADDR_MAX30102 = 0x57;

// ---------------------------------------------------------------------------
// Alert limits — placeholders only. TODO: tune against real data
// (readme.md #4).
// ---------------------------------------------------------------------------
constexpr float ALERT_HR_LOW_BPM   = 50.0f;
constexpr float ALERT_HR_HIGH_BPM  = 120.0f;
constexpr float ALERT_SPO2_LOW_PCT = 92.0f;
constexpr float ALERT_TEMP_LOW_C   = 35.0f;
constexpr float ALERT_TEMP_HIGH_C  = 38.5f;

// How long a dismissed alert stays dismissed before re-interrupting the
// display, if the underlying condition is still active (readme.md #5, #11).
// A tap or button press while the Alert screen is showing dismisses it back
// to normal navigation — added because the original full-screen-with-no-
// escape design made every other screen (and touch/button navigation
// entirely) unreachable for as long as the alert condition held, found
// during real end-to-end bring-up. Deliberately not "dismissed forever":
// a real ongoing condition (not just a one-off fall flag, which
// self-clears via kFallAlertDurationMs in motion_lis3dsh.h) should keep
// getting the wearer's attention periodically, not go silent after one tap.
// The status LED is unaffected by dismissal either way — it always
// reflects the real, current condition (see main.cpp's displayTask()).
constexpr uint32_t ALERT_ACK_COOLDOWN_MS = 30000;

// TODO: calibrate against a known-good reference (readme.md #3).
constexpr float TEMP_SKIN_TO_BODY_OFFSET_C = 2.0f;

// ---------------------------------------------------------------------------
// Touch navigation (readme.md #5) — see DEVELOPMENT.md for the design.
// ---------------------------------------------------------------------------

// Whether the touch panel's raw X/Y axes are swapped relative to the
// display's drawn coordinate system. This is a physical-mounting question
// (which way the touch overlay's film is oriented on the glass), not
// something derivable from the calibration math — genuinely unknown
// without a real unit. Flip and re-flash if the calibration crosshair
// doesn't track finger position correctly at bring-up (readme.md #11).
constexpr bool TOUCH_SWAP_XY = false;

// Inset, in pixels, from each screen edge for the two calibration targets.
// Keeps the crosshairs off the panel's least-linear extreme edges, where
// resistive touch response tends to be less reliable.
constexpr int16_t TOUCH_CAL_MARGIN = 20;

// Height, in pixels, reserved at the bottom of the screen for the touch
// nav bar. Every screen except the full-screen Alert draws around this.
constexpr int NAV_BAR_HEIGHT = 18;

// ---------------------------------------------------------------------------
// On-device Settings screen (readme.md #5). Scoped deliberately to
// brightness + sync-now — alert limits stay phone-app-only, since editing
// 5 numeric thresholds via tap-to-cycle on a 128x160 screen with no
// keyboard is disproportionate, and a stray tap silently changing a safety
// threshold is a real risk the phone app's actual input fields don't have.
// ---------------------------------------------------------------------------

// Discrete brightness levels the Settings screen cycles through by tap.
// SettingsStore persists the *index* into these arrays, not the raw
// percent, so the levels themselves can be retuned later without
// invalidating what's already stored in NVS on units in the field.
constexpr uint8_t BRIGHTNESS_LEVEL_COUNT = 3;
constexpr uint8_t BRIGHTNESS_PERCENTS[BRIGHTNESS_LEVEL_COUNT] = {30, 65, 100};
constexpr const char *BRIGHTNESS_LABELS[BRIGHTNESS_LEVEL_COUNT] = {"Low", "Med", "High"};

// Idle time before the screen and chip deep-sleep (readme.md #5). TODO:
// tune once real usage patterns are known, and expose on the Settings
// screen (readme.md #4).
constexpr uint32_t IDLE_TIMEOUT_MS = 15000;

// While a BLE client is connected, PowerMgr uses this longer timeout
// instead of IDLE_TIMEOUT_MS — otherwise the band would deep-sleep and
// drop the connection ~15s after the last button press even during active
// use (a real bug found during end-to-end review, not a hypothetical).
// Still finite rather than "never sleep while connected" so a
// backgrounded/stuck phone connection can't keep the band awake forever
// and drain the battery — 5 minutes is a starting guess, not measured.
constexpr uint32_t CONNECTED_IDLE_TIMEOUT_MS = 5 * 60 * 1000;

// ---------------------------------------------------------------------------
// FreeRTOS task tuning — see DEVELOPMENT.md "Task model". Priorities are on
// the standard FreeRTOS scale (0 = idle, higher = more urgent); the Arduino
// core's own loopTask runs at priority 1.
// ---------------------------------------------------------------------------
constexpr uint32_t TASK_STACK_SENSOR  = 4096;
constexpr uint32_t TASK_STACK_DISPLAY = 3072;
constexpr uint32_t TASK_STACK_BLE     = 4096;
constexpr uint32_t TASK_STACK_POWER   = 2048;
// Generous: WiFiClientSecure/HTTPUpdate pull in TLS, which is stack-hungry.
constexpr uint32_t TASK_STACK_OTA     = 8192;

constexpr uint8_t TASK_PRIORITY_SENSOR  = 3;
constexpr uint8_t TASK_PRIORITY_POWER   = 3;
constexpr uint8_t TASK_PRIORITY_BLE     = 2;
constexpr uint8_t TASK_PRIORITY_DISPLAY = 1;
// Rare and not time-critical — same tier as display.
constexpr uint8_t TASK_PRIORITY_OTA     = 1;
