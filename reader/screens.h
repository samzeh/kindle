// Which screen is showing, and the shared frame buffer they all draw into.
//
// Adding a screen: add an enum value, add a module with xxxShow/xxxTap, and
// add a case to each switch in app.cpp. A book settings screen (font,
// frontlight) is the next one planned.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

enum Screen : uint8_t {
  SCREEN_LIBRARY,
  SCREEN_READING,
};

// Switches screen and draws it with a full (flashing) refresh, which is what
// a whole-image change needs on e-paper.
void appGoTo(Screen s);

// The one 48 KB buffer every screen draws into. Never allocate another.
GFXcanvas1 &appCanvas();
