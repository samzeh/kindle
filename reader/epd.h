// Driver for the GDEQ0426T82 e-paper panel (SSD1677 controller), built from
// Good Display's demo code, which is known to work with this wiring.
//
// Images are 800 x 480, 1 bit per pixel, 100 bytes per row, most significant
// bit = leftmost pixel, 1 = white, 0 = black. A GFXcanvas1(800, 480) buffer
// has exactly this layout.
#pragma once
#include <Arduino.h>

static const uint16_t EPD_NATIVE_WIDTH = 800;
static const uint16_t EPD_NATIVE_HEIGHT = 480;

void epdBegin();

// Flashing refresh (~2 s). Clears ghosting; also required before the first
// partial refresh so the controller knows what is currently on screen.
void epdShowFull(const uint8_t *image);

// Non-flashing refresh that only changes pixels that differ from the
// previous image. Leaves slight ghosting over time.
void epdShowPartial(const uint8_t *image);

// Low-power mode. The image stays on screen; call epdShowFull() to wake.
void epdSleep();
