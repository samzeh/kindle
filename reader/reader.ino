// E-reader for the GDEQ0426T82 e-paper panel. See README.md.
//
// This file only connects the hardware to the reader logic in app.cpp, which
// is shared with the desktop simulator.
//
// Requires the Adafruit GFX library.

#include "board_config.h"
#include "app.h"
#include "epd.h"
#include "touch.h"

void setup() {
  Serial.begin(115200);

  pinMode(PIN_FRONTLIGHT, OUTPUT);
  digitalWrite(PIN_FRONTLIGHT, HIGH);

  epdBegin();
  touchBegin();
  appBegin();
}

void loop() {
  // Page turns can also be sent over USB serial: 'n' = next, 'p' = previous.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') appTurnPage(1);
    if (c == 'p') appTurnPage(-1);
  }

  uint16_t x, y;
  switch (touchPoll(x, y)) {
    case TOUCH_DOWN: appTap(x, y); break;
    case TOUCH_MOVE: appDrag(x, y); break;
    case TOUCH_UP: appTouchEnd(); break;
    case TOUCH_NONE: break;
  }
  delay(10);
}
