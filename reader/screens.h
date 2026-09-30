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

// Switches screen and draws it with the no-flash refresh. It redraws every
// changed pixel, so a whole new screen is fine, at the cost of a little more
// ghosting than a full (flashing) refresh would leave.
void appGoTo(Screen s);

// Which screen is showing. The reader itself never needs to ask -- appTap and
// appTurnPage already route by it -- but the simulator does, so a debug key
// cannot fire a tap at coordinates that mean something else on whichever
// screen happens to be up.
Screen appCurrentScreen();

// The one 48 KB buffer every screen draws into. Never allocate another.
GFXcanvas1 &appCanvas();
