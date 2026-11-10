// The reader's display settings -- font size, frontlight brightness and dark
// mode -- their persistence, and the body fonts a chosen size selects.
//
// Values live here rather than in the settings sheet so that layout.cpp,
// reading.cpp and app.cpp can read them without depending on the UI that
// changes them. The sheet sets; everything else reads.
#pragma once
#include <stdint.h>

#include "layout.h"  // PageFonts

// The font-size setting's seven steps, in points.
static const uint8_t PREFS_FONT_COUNT = 7;
static const uint8_t PREFS_FONT_DEFAULT = 3;  // 12pt, the size the reader shipped with

static const uint8_t PREFS_BRIGHTNESS_DEFAULT = 60;
static const uint8_t PREFS_BRIGHTNESS_STEP = 5;  // tapping the track snaps to this

// Every setting that can move a line break, packed so one hash identifies the
// whole set. Add a field here and every book re-paginates when it is next
// opened, because its stored key no longer matches -- no change to the cache
// format, no migration. Margins and line spacing belong here if they are ever
// added.
struct LayoutInputs {
  uint8_t fontSize;
};

// Loads the settings and applies the frontlight. Call from appBegin, after
// storeBegin.
void prefsBegin();

uint8_t prefsFontSize();              // 0 .. PREFS_FONT_COUNT - 1
void prefsSetFontSize(uint8_t index);  // clamped; does not save
uint8_t prefsFontPoints(uint8_t index);

uint8_t prefsBrightness();             // 0 .. 100
void prefsSetBrightness(uint8_t percent);  // applies to the light at once; does not save

bool prefsDarkMode();
void prefsSetDarkMode(bool on);        // does not save

// The body fonts for the current size, and for any size (so the settings
// sheet can draw a preview of one it has not committed to).
const PageFonts &prefsBookFonts();
const PageFonts &prefsBookFontsAt(uint8_t index);

// Identifies the pagination-affecting settings. A book's pages.bin stores the
// value in force when it was counted; a mismatch means re-count.
uint32_t prefsLayoutKey();

// Writes all three to the store. The settings sheet calls this once, as it
// closes, so changing a setting repeatedly costs one flash write.
void prefsSave();
