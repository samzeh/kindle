#include "prefs.h"

#include "fonts/FreeSerif9pt7b.h"
#include "fonts/FreeSerifItalic9pt7b.h"
#include "fonts/FreeSerifBold9pt7b.h"
#include "fonts/FreeSerifBoldItalic9pt7b.h"
#include "fonts/FreeSerif10pt7b.h"
#include "fonts/FreeSerifItalic10pt7b.h"
#include "fonts/FreeSerifBold10pt7b.h"
#include "fonts/FreeSerifBoldItalic10pt7b.h"
#include "fonts/FreeSerif11pt7b.h"
#include "fonts/FreeSerifItalic11pt7b.h"
#include "fonts/FreeSerifBold11pt7b.h"
#include "fonts/FreeSerifBoldItalic11pt7b.h"
#include "fonts/FreeSerif12pt7b.h"
#include "fonts/FreeSerifItalic12pt7b.h"
#include "fonts/FreeSerifBold12pt7b.h"
#include "fonts/FreeSerifBoldItalic12pt7b.h"
#include "fonts/FreeSerif14pt7b.h"
#include "fonts/FreeSerifItalic14pt7b.h"
#include "fonts/FreeSerifBold14pt7b.h"
#include "fonts/FreeSerifBoldItalic14pt7b.h"
#include "fonts/FreeSerif16pt7b.h"
#include "fonts/FreeSerifItalic16pt7b.h"
#include "fonts/FreeSerifBold16pt7b.h"
#include "fonts/FreeSerifBoldItalic16pt7b.h"
#include "fonts/FreeSerif18pt7b.h"
#include "fonts/FreeSerifItalic18pt7b.h"
#include "fonts/FreeSerifBold18pt7b.h"
#include "fonts/FreeSerifBoldItalic18pt7b.h"

#include "frontlight.h"
#include "hash.h"
#include "store.h"

static const uint8_t FONT_POINTS[PREFS_FONT_COUNT] = { 9, 10, 11, 12, 14, 16, 18 };

static const PageFonts FONTS[PREFS_FONT_COUNT] = {
  { &FreeSerif9pt7b, &FreeSerifItalic9pt7b, &FreeSerifBold9pt7b, &FreeSerifBoldItalic9pt7b },
  { &FreeSerif10pt7b, &FreeSerifItalic10pt7b, &FreeSerifBold10pt7b, &FreeSerifBoldItalic10pt7b },
  { &FreeSerif11pt7b, &FreeSerifItalic11pt7b, &FreeSerifBold11pt7b, &FreeSerifBoldItalic11pt7b },
  { &FreeSerif12pt7b, &FreeSerifItalic12pt7b, &FreeSerifBold12pt7b, &FreeSerifBoldItalic12pt7b },
  { &FreeSerif14pt7b, &FreeSerifItalic14pt7b, &FreeSerifBold14pt7b, &FreeSerifBoldItalic14pt7b },
  { &FreeSerif16pt7b, &FreeSerifItalic16pt7b, &FreeSerifBold16pt7b, &FreeSerifBoldItalic16pt7b },
  { &FreeSerif18pt7b, &FreeSerifItalic18pt7b, &FreeSerifBold18pt7b, &FreeSerifBoldItalic18pt7b },
};

static LayoutInputs inputs = { PREFS_FONT_DEFAULT };
static uint8_t brightness = PREFS_BRIGHTNESS_DEFAULT;
static bool darkMode = false;

void prefsBegin() {
  inputs.fontSize = storeLoadFontSize(PREFS_FONT_DEFAULT);
  if (inputs.fontSize >= PREFS_FONT_COUNT) inputs.fontSize = PREFS_FONT_DEFAULT;
  brightness = storeLoadBrightness(PREFS_BRIGHTNESS_DEFAULT);
  if (brightness > 100) brightness = PREFS_BRIGHTNESS_DEFAULT;
  darkMode = storeLoadDarkMode(false);
  frontlightBegin();
  frontlightSet(brightness);
}

uint8_t prefsFontSize() {
  return inputs.fontSize;
}

void prefsSetFontSize(uint8_t index) {
  inputs.fontSize = index < PREFS_FONT_COUNT ? index : PREFS_FONT_COUNT - 1;
}

uint8_t prefsFontPoints(uint8_t index) {
  return FONT_POINTS[index < PREFS_FONT_COUNT ? index : PREFS_FONT_COUNT - 1];
}

uint8_t prefsBrightness() {
  return brightness;
}

void prefsSetBrightness(uint8_t percent) {
  brightness = percent > 100 ? 100 : percent;
  // Applied straight away: the only way to judge a brightness is to see it.
  frontlightSet(brightness);
}

bool prefsDarkMode() {
  return darkMode;
}

void prefsSetDarkMode(bool on) {
  darkMode = on;
}

const PageFonts &prefsBookFonts() {
  return FONTS[inputs.fontSize];
}

const PageFonts &prefsBookFontsAt(uint8_t index) {
  return FONTS[index < PREFS_FONT_COUNT ? index : PREFS_FONT_COUNT - 1];
}

uint32_t prefsLayoutKey() {
  return fnv1a(&inputs, sizeof(inputs));
}

void prefsSave() {
  storeSaveFontSize(inputs.fontSize);
  storeSaveBrightness(brightness);
  storeSaveDarkMode(darkMode);
}
