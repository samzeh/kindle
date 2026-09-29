// The screen router: owns the single frame buffer, normalises raw touch
// coordinates once, and dispatches taps to whichever screen is showing.
//
// It has no hardware code: it draws through epd.h and is handed taps, so the
// same code runs on the ESP32 (reader.ino) and in the desktop simulator
// (../simulator).
#pragma once
#include <stdint.h>

// Shows the first page with a full refresh.
void appBegin();

// A tap at raw touch-panel coordinates.
void appTap(uint16_t x, uint16_t y);

// +1 = next page, -1 = previous page.
void appTurnPage(int delta);
