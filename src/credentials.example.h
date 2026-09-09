#pragma once

// Template for src/credentials.h, which is gitignored (see .gitignore and
// DEVELOPMENT.md "Secrets"). Copy this file to src/credentials.h and fill in
// real values there — never edit real credentials into this tracked copy.
//
//   cp src/credentials.example.h src/credentials.h
//
// Only used by WifiSync::startOtaUpdate() (readme.md #7); not needed to
// build or flash the rest of the firmware.

constexpr const char *kWifiSsid = "TODO_SSID";
constexpr const char *kWifiPassword = "TODO_PASSWORD";
constexpr const char *kOtaUpdateUrl = "http://TODO_HOST/firmware.bin";
