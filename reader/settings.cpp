#include "settings.h"

#include <stdio.h>

#include "cover.h"  // drawCentredText, truncateToWidth
#include "fonts/FreeSans8pt7b.h"
#include "fonts/FreeSans9pt7b.h"
#include "icons.h"
#include "prefs.h"

static const uint16_t INK = 0x0000, PAPER = 0xFFFF;
static const int16_t SCREEN_W = 480;

// ---- Geometry ----
// The sheet is a card inset from three edges, near the top because that is
// where the gear that opens it lives. Every tappable row is at least 44 px
// tall. These constants are the hit tests' only source of truth as well as
// the drawing's, so the two cannot drift apart.

static const int16_t SHEET_X = 12;
static const int16_t SHEET_Y = 12;
static const int16_t SHEET_W = SCREEN_W - 2 * SHEET_X;  // 456
// Tall enough to clear the dark-mode subtitle's descenders before the grab
// handle's band starts; the handle needs its own room or it draws a line
// through the text above it.
static const int16_t SHEET_H = 352;
static const int16_t SHEET_R = 12;  // corner radius
static const int16_t SHEET_BOTTOM = SHEET_Y + SHEET_H;

static const int16_t PAD = 20;
static const int16_t LEFT = SHEET_X + PAD;             // 32
static const int16_t RIGHT = SHEET_X + SHEET_W - PAD;  // 448

// Header: just the title, left-aligned like the library's own header. There
// is nothing on the right because there is nothing to confirm.
static const int16_t HEAD_TOP = SHEET_Y;
static const int16_t HEAD_H = 56;
static const int16_t HEAD_MID = HEAD_TOP + HEAD_H / 2;

// The two slider rows share a track, so the knob and the dots line up.
static const int16_t END_BOX = 44;  // the A boxes and the sun icons' square
static const int16_t TRACK_X = LEFT + END_BOX + 16;          // 92
static const int16_t TRACK_W = RIGHT - END_BOX - 16 - TRACK_X;  // 296
static const int16_t TRACK_END = TRACK_X + TRACK_W;          // 388

static const int16_t FONT_LABEL_BASE = SHEET_Y + 88;
static const int16_t FONT_MID = SHEET_Y + 130;
static const int16_t BRIGHT_LABEL_BASE = SHEET_Y + 180;
static const int16_t BRIGHT_MID = SHEET_Y + 218;
static const int16_t DIVIDER_Y = SHEET_Y + 252;
static const int16_t DARK_MID = SHEET_Y + 288;
static const int16_t DARK_TOP = DIVIDER_Y + 1;

// Rows are bands, so a tap near a control still counts as hitting it.
static const int16_t ROW_REACH = 28;

static const int16_t TOGGLE_W = 58, TOGGLE_H = 32;
static const int16_t TOGGLE_X = RIGHT - TOGGLE_W;

// The grab handle across the bottom edge: the only thing on the sheet that
// says how to get rid of it, since there is no button to press. Touching it
// closes the sheet -- a handle that looked draggable and did nothing would be
// worse than no handle at all.
static const int16_t HANDLE_W = 72, HANDLE_H = 5;
static const int16_t HANDLE_Y = SHEET_BOTTOM - 14;
static const int16_t HANDLE_TOP = SHEET_BOTTOM - 26;  // its band, not just the bar

// ---- State ----

static bool open_ = false;
// The layout key when the sheet opened, so closing knows whether anything
// that moves a line break actually changed.
static uint32_t openLayoutKey = 0;

// Which slider a finger landed on, so that dragging off the track sideways
// keeps controlling the slider it started on rather than jumping to the
// other one.
enum Dragging : uint8_t { DRAG_NONE, DRAG_FONT, DRAG_BRIGHTNESS };
static Dragging dragging = DRAG_NONE;

bool settingsIsOpen() {
  return open_;
}

int16_t settingsSheetBottom() {
  return SHEET_BOTTOM;
}

void settingsOpen() {
  openLayoutKey = prefsLayoutKey();
  dragging = DRAG_NONE;
  open_ = true;
}

// ---- Hit testing ----

// Which of the seven dots a tap at x is nearest. The track's ends are the
// first and last dot's centres, so the outer halves of the end dots are off
// the track and still have to select them.
static uint8_t fontIndexAt(int16_t x) {
  int16_t span = TRACK_W;
  int32_t rel = (int32_t)(x - TRACK_X) * (PREFS_FONT_COUNT - 1);
  int16_t index = (int16_t)((rel + span / 2) / span);  // nearest, not floor
  if (index < 0) index = 0;
  if (index >= PREFS_FONT_COUNT) index = PREFS_FONT_COUNT - 1;
  return (uint8_t)index;
}

static uint8_t brightnessAt(int16_t x) {
  if (x <= TRACK_X) return 0;
  if (x >= TRACK_END) return 100;
  int16_t pct = (int16_t)(((int32_t)(x - TRACK_X) * 100 + TRACK_W / 2) / TRACK_W);
  // Snapped, so tapping the same place twice cannot report two values.
  int16_t step = PREFS_BRIGHTNESS_STEP;
  pct = (pct + step / 2) / step * step;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

SettingsHitResult settingsHitTest(int16_t x, int16_t y) {
  if (x < SHEET_X || x >= SHEET_X + SHEET_W || y < SHEET_Y || y >= SHEET_BOTTOM)
    return { HIT_OUTSIDE, 0 };

  if (y < HEAD_TOP + HEAD_H) return { HIT_NONE, 0 };
  // Checked before the dark row, which otherwise runs to the sheet's edge and
  // would swallow the handle.
  if (y >= HANDLE_TOP) return { HIT_CLOSE, 0 };
  // The dark-mode row is the whole width below the divider: a toggle is a
  // small target, and the label beside it means the same thing.
  if (y >= DARK_TOP) return { HIT_DARK, 0 };
  if (y >= BRIGHT_MID - ROW_REACH && y < BRIGHT_MID + ROW_REACH)
    return { HIT_BRIGHTNESS, brightnessAt(x) };
  if (y >= FONT_MID - ROW_REACH && y < FONT_MID + ROW_REACH)
    return { HIT_FONT, fontIndexAt(x) };
  return { HIT_NONE, 0 };
}

static SettingsTap nothing() {
  return { true, false, false, false, false, false };
}

// Closes the sheet, saving as it goes. The save happens here rather than on
// every tap so that sliding the brightness across its whole range costs one
// flash write instead of twenty.
static SettingsTap close() {
  SettingsTap r = nothing();
  prefsSave();
  r.closed = true;
  r.relayout = prefsLayoutKey() != openLayoutKey;
  dragging = DRAG_NONE;
  open_ = false;
  return r;
}

SettingsTap settingsTap(int16_t x, int16_t y) {
  SettingsTap r = nothing();
  if (!open_) {
    r.handled = false;
    return r;
  }
  SettingsHitResult h = settingsHitTest(x, y);
  switch (h.hit) {
    case HIT_FONT:
      dragging = DRAG_FONT;
      if (h.value != prefsFontSize()) {
        prefsSetFontSize(h.value);
        r.previewFonts = true;
        r.redraw = true;
      }
      break;
    case HIT_BRIGHTNESS:
      dragging = DRAG_BRIGHTNESS;
      // Applied even when the value is unchanged: harmless, and it means a
      // touch on the track always does something on the device.
      prefsSetBrightness(h.value);
      r.redraw = true;
      break;
    case HIT_DARK:
      prefsSetDarkMode(!prefsDarkMode());
      // A whole screen going from mostly white to mostly black leaves heavy
      // ghosting under the no-flash waveform.
      r.fullRefresh = true;
      r.redraw = true;
      break;
    case HIT_OUTSIDE:
    case HIT_CLOSE:
      return close();
    case HIT_NONE:
      break;
  }
  return r;
}

// Following a finger, which only the sliders do.
//
// Nothing is redrawn here. The panel takes a few hundred milliseconds per
// refresh, so redrawing per step would make the sheet lag a finger badly and
// ghost while it did it. Brightness does not need the redraw -- the light
// itself follows, which is what is being judged -- and the screen catches up
// in one refresh when the finger lifts.
SettingsTap settingsDrag(int16_t x, int16_t y) {
  SettingsTap r = nothing();
  if (!open_ || dragging == DRAG_NONE) {
    r.handled = open_;
    return r;
  }
  if (dragging == DRAG_BRIGHTNESS) {
    prefsSetBrightness(brightnessAt(x));
  } else {
    prefsSetFontSize(fontIndexAt(x));
  }
  (void)y;  // a slider follows x only, so straying off the track is forgiven
  return r;
}

SettingsTap settingsTouchEnd() {
  SettingsTap r = nothing();
  if (!open_ || dragging == DRAG_NONE) {
    r.handled = open_;
    dragging = DRAG_NONE;
    return r;
  }
  // The one redraw of a drag: the knob, and the number beside it, move to
  // where the finger left them.
  r.redraw = true;
  r.previewFonts = dragging == DRAG_FONT;
  dragging = DRAG_NONE;
  return r;
}

// ---- Drawing ----

// Baseline that puts a capital letter's middle on `mid`, so text lines up
// with the icons beside it.
static int16_t baselineFor(const GFXfont &font, int16_t mid) {
  const GFXglyph &cap = font.glyph['H' - font.first];
  return mid - cap.yOffset - (cap.height - 1) / 2;
}

static void drawLeftText(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                         int16_t x, int16_t baseline) {
  gfx.setFont(font);
  gfx.setCursor(x, baseline);
  gfx.print(text);
}

// Right-aligned by measurement, so the readouts stay put as "9 of 9" becomes
// "10 of 9" or 5% becomes 100%.
static void drawRightText(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                          int16_t right, int16_t baseline) {
  gfx.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(right - (int16_t)w - x1, baseline);
  gfx.print(text);
}

// A track the knob has not reached. There is no grey on a 1-bit panel, so
// "lighter" has to be dots: solid means filled, dotted means still to go.
static void drawDottedLine(Adafruit_GFX &gfx, int16_t x0, int16_t x1, int16_t y) {
  for (int16_t x = x0; x <= x1; x += 3) gfx.drawPixel(x, y, INK);
}

static void drawKnob(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  gfx.fillCircle(cx, cy, 11, PAPER);
  gfx.drawCircle(cx, cy, 11, INK);
  gfx.drawCircle(cx, cy, 10, INK);  // 2 px, so it reads at arm's length
}

// The letter A at the size it stands for, in a rounded box: an honest preview
// of the typeface, not an icon of one.
static void drawFontEndBox(Adafruit_GFX &gfx, int16_t x, int16_t mid, const GFXfont *font) {
  gfx.drawRoundRect(x, mid - END_BOX / 2, END_BOX, END_BOX, 6, INK);
  gfx.setTextColor(INK);
  drawCentredText(gfx, "A", font, x, END_BOX, baselineFor(*font, mid));
}

static void drawHeader(Adafruit_GFX &gfx) {
  gfx.setTextColor(INK);
  drawLeftText(gfx, "Display", &FreeSans9pt7b, LEFT, baselineFor(FreeSans9pt7b, HEAD_MID));
}

// The handle across the bottom edge. With no button to dismiss the sheet, it
// is the one thing that says the sheet is a thing sitting on top of the page
// rather than part of it -- and so that tapping the page gets rid of it.
static void drawHandle(Adafruit_GFX &gfx) {
  gfx.fillRoundRect(SHEET_X + (SHEET_W - HANDLE_W) / 2, HANDLE_Y,
                    HANDLE_W, HANDLE_H, HANDLE_H / 2, INK);
}

static void drawFontRow(Adafruit_GFX &gfx) {
  char label[16];
  snprintf(label, sizeof(label), "%u of %u", (unsigned)prefsFontSize() + 1,
           (unsigned)PREFS_FONT_COUNT);
  int16_t base = baselineFor(FreeSans9pt7b, FONT_LABEL_BASE);
  gfx.setTextColor(INK);
  drawLeftText(gfx, "Font size", &FreeSans9pt7b, LEFT, base);
  drawRightText(gfx, label, &FreeSans8pt7b, RIGHT, base);

  drawFontEndBox(gfx, LEFT, FONT_MID, &FreeSans8pt7b);
  drawFontEndBox(gfx, RIGHT - END_BOX, FONT_MID, &FreeSans9pt7b);

  const uint8_t index = prefsFontSize();
  for (uint8_t i = 0; i < PREFS_FONT_COUNT; i++) {
    int16_t cx = TRACK_X + (int16_t)((int32_t)TRACK_W * i / (PREFS_FONT_COUNT - 1));
    if (i < index) {
      if (i) gfx.drawFastHLine(TRACK_X, FONT_MID, cx - TRACK_X, INK);
      gfx.fillCircle(cx, FONT_MID, 3, INK);
    } else if (i > index) {
      gfx.fillCircle(cx, FONT_MID, 2, INK);
    }
  }
  int16_t knobX = TRACK_X + (int16_t)((int32_t)TRACK_W * index / (PREFS_FONT_COUNT - 1));
  gfx.drawFastHLine(TRACK_X, FONT_MID, knobX - TRACK_X, INK);
  drawDottedLine(gfx, knobX, TRACK_END, FONT_MID);
  drawKnob(gfx, knobX, FONT_MID);
}

static void drawBrightnessRow(Adafruit_GFX &gfx) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", (unsigned)prefsBrightness());
  int16_t base = baselineFor(FreeSans9pt7b, BRIGHT_LABEL_BASE);
  gfx.setTextColor(INK);
  drawLeftText(gfx, "Brightness", &FreeSans9pt7b, LEFT, base);
  drawRightText(gfx, label, &FreeSans8pt7b, RIGHT, base);

  iconSun(gfx, LEFT + END_BOX / 2, BRIGHT_MID, 6);
  iconSun(gfx, RIGHT - END_BOX / 2, BRIGHT_MID, 12);

  int16_t knobX = TRACK_X + (int16_t)((int32_t)TRACK_W * prefsBrightness() / 100);
  for (int16_t t = -1; t <= 1; t++)  // 3 px: the filled part is the value
    gfx.drawFastHLine(TRACK_X, BRIGHT_MID + t, knobX - TRACK_X, INK);
  drawDottedLine(gfx, knobX, TRACK_END, BRIGHT_MID);
  drawKnob(gfx, knobX, BRIGHT_MID);
}

static void drawToggle(Adafruit_GFX &gfx, int16_t x, int16_t mid, bool on) {
  int16_t y = mid - TOGGLE_H / 2, r = TOGGLE_H / 2;
  if (on) {
    gfx.fillRoundRect(x, y, TOGGLE_W, TOGGLE_H, r, INK);
    gfx.fillCircle(x + TOGGLE_W - r, mid, r - 5, PAPER);
  } else {
    gfx.drawRoundRect(x, y, TOGGLE_W, TOGGLE_H, r, INK);
    gfx.fillCircle(x + r, mid, r - 5, INK);
  }
}

static void drawDarkRow(Adafruit_GFX &gfx) {
  iconMoon(gfx, LEFT + END_BOX / 2, DARK_MID);
  gfx.setTextColor(INK);
  // Hierarchy by size, since a 1-bit panel has no lighter ink to set a
  // subtitle in.
  drawLeftText(gfx, "Dark mode", &FreeSans9pt7b, TRACK_X, DARK_MID - 2);
  drawLeftText(gfx, "Light text on a black page", &FreeSans8pt7b, TRACK_X, DARK_MID + 18);
  drawToggle(gfx, TOGGLE_X, DARK_MID, prefsDarkMode());
}

void settingsDraw(Adafruit_GFX &gfx) {
  if (!open_) return;
  gfx.fillRoundRect(SHEET_X, SHEET_Y, SHEET_W, SHEET_H, SHEET_R, PAPER);
  gfx.drawRoundRect(SHEET_X, SHEET_Y, SHEET_W, SHEET_H, SHEET_R, INK);

  drawHeader(gfx);
  gfx.drawFastHLine(LEFT, HEAD_TOP + HEAD_H, RIGHT - LEFT, INK);
  drawFontRow(gfx);
  drawBrightnessRow(gfx);
  gfx.drawFastHLine(LEFT, DIVIDER_Y, RIGHT - LEFT, INK);
  drawDarkRow(gfx);
  drawHandle(gfx);
}
