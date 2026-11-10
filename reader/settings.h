// The display settings sheet: font size, frontlight brightness and dark mode,
// on a card that drops from the top of whichever screen is showing.
//
// It is an overlay rather than a Screen (screens.h): the library or the book
// stays underneath, and app.cpp draws the sheet over it and offers taps to it
// first. That way one implementation serves both screens and neither has to
// know the sheet exists.
//
// There is no Done and no Cancel. A setting takes effect when you change it
// and is saved when the sheet closes, which is what the controls already look
// like they do -- a switch that needed confirming afterwards would be lying
// about itself. Closing is tapping anywhere outside the sheet.
//
// The sliders can be dragged as well as tapped. Brightness is the reason: the
// front light follows a finger instantly because dimming an LED costs nothing,
// while the panel redraws only once, when the finger lifts. So the thing being
// judged keeps up even though the screen cannot.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

// What a touch on the sheet did. The sheet changes prefs itself but does not
// know how to redraw a page or re-count a book, so it says what happened and
// app.cpp acts on it.
struct SettingsTap {
  bool handled;       // the touch was the sheet's; it must not reach the screen below
  bool closed;        // the sheet is no longer showing; its settings have been saved
  bool relayout;      // a pagination-affecting setting moved while it was open
  bool previewFonts;  // the font size changed: redraw the page under the sheet
  bool redraw;        // the sheet's own drawing is out of date
  bool fullRefresh;   // dark mode changed: the next refresh must be a flashing one
};

bool settingsIsOpen();

// The sheet's bottom edge, so the screen underneath can clear whole lines
// down to it rather than letting the sheet slice one in half.
int16_t settingsSheetBottom();

void settingsOpen();

void settingsDraw(Adafruit_GFX &gfx);

// A finger landing, a finger that has stayed down and moved, and the moment
// it lifts. settingsDrag only ever continues what settingsTap started, so a
// drag that began outside the sheet cannot reach into it.
SettingsTap settingsTap(int16_t x, int16_t y);
SettingsTap settingsDrag(int16_t x, int16_t y);
SettingsTap settingsTouchEnd();

// ---- For the tests ----
// What a touch at (x, y) means, without acting on it.
enum SettingsHit : uint8_t {
  HIT_NONE,        // inside the sheet, on nothing
  HIT_OUTSIDE,     // outside the sheet: close it
  HIT_CLOSE,       // the grab handle: closes too, so it does what it looks like
  HIT_FONT,        // value = the font-size index touched
  HIT_BRIGHTNESS,  // value = the percentage touched, already quantised
  HIT_DARK,
};

struct SettingsHitResult {
  SettingsHit hit;
  uint8_t value;
};

SettingsHitResult settingsHitTest(int16_t x, int16_t y);
