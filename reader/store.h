// Saved reading positions and the chosen library view, kept in the ESP32's
// NVS flash so they survive a reboot.
//
// store.cpp uses the ESP32's Preferences library, so it is a hardware-only
// file: the simulator provides its own implementation in main.cpp, backed by
// a file, so it behaves the same way.
#pragma once
#include <stdint.h>

// Offset and italic travel as one uint32: offset in bits 0-30, italic in bit
// 31. One NVS entry per save rather than two, and one key per book. Text
// lengths are nowhere near 2^31. These two are pure, so they are tested; they
// are inline so they stay testable on the host even though store.cpp itself
// is hardware-only and not linked into the host test target.
inline uint32_t storePack(uint32_t offset, bool italic) {
  return (offset & 0x7FFFFFFFu) | (italic ? 0x80000000u : 0u);
}

inline void storeUnpack(uint32_t packed, uint32_t &offset, bool &italic) {
  offset = packed & 0x7FFFFFFFu;
  italic = (packed & 0x80000000u) != 0;
}

void storeBegin();
void storeSaveProgress(uint8_t book, uint32_t offset, bool italic);
bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic);
void storeSaveView(uint8_t view);   // 0 = grid, 1 = list
uint8_t storeLoadView();            // 0 (grid) if never set
