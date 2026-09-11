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

// Motion wake — RTC-capable, required for deep-sleep wake (readme.md #5)
constexpr int PIN_LIS3DH_INT1 = 1;

// Buttons (to GND, internal pull-up).
// Both moved off the ESP32-S3 SuperMini's non-header pads (readme.md #8.3
// "Pins to keep clear") onto pins actually reachable without pogo pins.
// GPIO3 and GPIO15 are strapping-adjacent/"system duty" pins per the
// board's pinout reference, not hazard-free — see the note in readme.md
// #8.3. Don't hold either button down while powering on or resetting the
// board: GPIO3 selects the JTAG interface at boot.
constexpr int PIN_BUTTON_1 = 3;   // RTC-capable; doubles as a wake source
constexpr int PIN_BUTTON_2 = 15;

// Status LED (on-board WS2812)
constexpr int PIN_STATUS_LED = 48;

// Optional interrupt lines
constexpr int PIN_MAX30102_INT  = 40;
constexpr int PIN_MAX17048_ALRT = 38;

// ---------------------------------------------------------------------------
// I2C addresses — mirrors readme.md #8.4.
// ---------------------------------------------------------------------------
constexpr uint8_t I2C_ADDR_LIS3DH   = 0x18;  // or 0x19, depending on SDO strap
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

// TODO: calibrate against a known-good reference (readme.md #3).
constexpr float TEMP_SKIN_TO_BODY_OFFSET_C = 2.0f;

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
