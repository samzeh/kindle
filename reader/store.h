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
