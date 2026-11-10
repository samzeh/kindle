// Saved reading positions and the chosen library view, kept in the ESP32's
// NVS flash so they survive a reboot.
//
// store.cpp uses the ESP32's Preferences library, so it is a hardware-only
// file: the simulator and tests use simulator/host_platform.cpp instead.
#pragma once
#include <stdint.h>

void storeBegin();
// Positions are text offsets (see catalog.h), keyed by book id, so adding or
// removing books never mixes them up.
void storeSaveProgress(uint32_t bookId, uint32_t offset);
bool storeLoadProgress(uint32_t bookId, uint32_t &offset);
void storeSaveView(uint8_t view);   // 0 = grid, 1 = list
uint8_t storeLoadView();            // 0 (grid) if never set

// The display settings (see prefs.h), written once when the settings sheet
// closes rather than on every change, so sliding a slider across its range
// costs one flash write instead of twenty. The defaults are passed in rather
// than hardcoded here: what a sensible font size is belongs with the font
// table, not with the key-value store.
void storeSaveFontSize(uint8_t index);
uint8_t storeLoadFontSize(uint8_t fallback);
void storeSaveBrightness(uint8_t percent);
uint8_t storeLoadBrightness(uint8_t fallback);
void storeSaveDarkMode(bool on);
bool storeLoadDarkMode(bool fallback);
