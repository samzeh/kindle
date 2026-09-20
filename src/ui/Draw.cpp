#include "ui/Draw.h"

#include "hal/Display.h"
#include "text/Content.h"

namespace ui {

int16_t textWidth(const char* s, FontStyle style, uint16_t px) {
  gFonts.setSize(px);
  size_t len = strlen(s);
  size_t i = 0;
  int16_t w = 0;
  while (i < len) {
    uint32_t cp = utf8Next(s, len, &i);
    if (cp == 0) break;
    w += gFonts.advance(style, cp);
  }
  return w;
}

int16_t drawText(int16_t x, int16_t baseline, const char* s, FontStyle style,
                 uint16_t px) {
  gFonts.setSize(px);
  size_t len = strlen(s);
  size_t i = 0;
  while (i < len) {
    uint32_t cp = utf8Next(s, len, &i);
    if (cp == 0) break;

    const Glyph* g = gFonts.glyph(style, cp);
    if (g) {
      gDisplay.gfx().drawBitmap(x + g->xoff, baseline + g->yoff,
                                const_cast<uint8_t*>(g->bits), g->w, g->h,
                                GxEPD_BLACK);
      x += g->advance;
    } else {
      x += gFonts.advance(style, cp);
    }
  }
  return x;
}

void drawTextCentered(int16_t centerX, int16_t baseline, const char* s,
                      FontStyle style, uint16_t px) {
  int16_t w = textWidth(s, style, px);
  drawText(centerX - w / 2, baseline, s, style, px);
}

void drawTextClipped(int16_t x, int16_t baseline, int16_t maxWidth, const char* s,
                     FontStyle style, uint16_t px) {
  gFonts.setSize(px);
  if (textWidth(s, style, px) <= maxWidth) {
    drawText(x, baseline, s, style, px);
    return;
  }

  int16_t ellipsisW = gFonts.advance(style, 0x2026);
  int16_t budget = maxWidth - ellipsisW;
  size_t len = strlen(s);
  size_t i = 0;
  int16_t pen = x;

  while (i < len) {
    size_t before = i;
    uint32_t cp = utf8Next(s, len, &i);
    if (cp == 0) break;

    int16_t adv = gFonts.advance(style, cp);
    if (pen + adv - x > budget) {
      i = before;
      break;
    }
    const Glyph* g = gFonts.glyph(style, cp);
    if (g) {
      gDisplay.gfx().drawBitmap(pen + g->xoff, baseline + g->yoff,
                                const_cast<uint8_t*>(g->bits), g->w, g->h,
                                GxEPD_BLACK);
    }
    pen += adv;
  }

  const Glyph* dots = gFonts.glyph(style, 0x2026);
  if (dots) {
    gDisplay.gfx().drawBitmap(pen + dots->xoff, baseline + dots->yoff,
                              const_cast<uint8_t*>(dots->bits), dots->w, dots->h,
                              GxEPD_BLACK);
  }
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black) {
  gDisplay.gfx().fillRect(x, y, w, h, black ? GxEPD_BLACK : GxEPD_WHITE);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  gDisplay.gfx().drawRect(x, y, w, h, GxEPD_BLACK);
}

void drawHLine(int16_t x, int16_t y, int16_t w) {
  gDisplay.gfx().drawFastHLine(x, y, w, GxEPD_BLACK);
}

void drawListRow(int16_t y, int16_t h, const char* label, const char* value,
                 bool selected, uint16_t px) {
  constexpr int16_t PAD = 18;

  if (selected) {
    // Inverted rows are cheap on e-ink and read clearly without colour.
    fillRect(0, y, SCREEN_W, h, true);
  }

  gFonts.setSize(px);
  int16_t baseline = y + (h + gFonts.ascent()) / 2 - 2;

  int16_t valueW = 0;
  if (value && *value) valueW = textWidth(value, FontStyle::Regular, px) + PAD;

  uint16_t colour = selected ? GxEPD_WHITE : GxEPD_BLACK;
  // Selected rows invert, so temporarily draw glyphs in white by painting the
  // row black first and relying on the display's two-colour model.
  if (selected) {
    // Adafruit_GFX's drawBitmap only paints set bits, so white text needs the
    // explicit colour form.
    gFonts.setSize(px);
    size_t len = strlen(label);
    size_t i = 0;
    int16_t x = PAD;
    int16_t limit = SCREEN_W - PAD - valueW;
    while (i < len) {
      uint32_t cp = utf8Next(label, len, &i);
      if (cp == 0) break;
      const Glyph* g = gFonts.glyph(FontStyle::Regular, cp);
      if (!g) continue;
      if (x + g->advance > limit) break;
      gDisplay.gfx().drawBitmap(x + g->xoff, baseline + g->yoff,
                                const_cast<uint8_t*>(g->bits), g->w, g->h, colour);
      x += g->advance;
    }
    if (value && *value) {
      int16_t vx = SCREEN_W - PAD - textWidth(value, FontStyle::Regular, px);
      size_t vlen = strlen(value);
      size_t vi = 0;
      while (vi < vlen) {
        uint32_t cp = utf8Next(value, vlen, &vi);
        if (cp == 0) break;
        const Glyph* g = gFonts.glyph(FontStyle::Regular, cp);
        if (!g) continue;
        gDisplay.gfx().drawBitmap(vx + g->xoff, baseline + g->yoff,
                                  const_cast<uint8_t*>(g->bits), g->w, g->h, colour);
        vx += g->advance;
      }
    }
  } else {
    drawTextClipped(PAD, baseline, SCREEN_W - 2 * PAD - valueW, label,
                    FontStyle::Regular, px);
    if (value && *value) {
      int16_t vx = SCREEN_W - PAD - textWidth(value, FontStyle::Regular, px);
      drawText(vx, baseline, value, FontStyle::Regular, px);
    }
    drawHLine(PAD, y + h - 1, SCREEN_W - 2 * PAD);
  }
}

void drawMessage(const char* title, const char* detail) {
  gDisplay.clearBuffer();
  drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 - 20, title, FontStyle::Bold, 30);
  if (detail && *detail) {
    drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 + 20, detail, FontStyle::Regular, 22);
  }
}

void drawProgress(const char* title, const char* stage, uint16_t done,
                  uint16_t total) {
  constexpr int16_t BAR_W = 300;
  constexpr int16_t BAR_H = 14;

  gDisplay.clearBuffer();

  int16_t cy = SCREEN_H / 2;
  drawTextClipped(40, cy - 70, SCREEN_W - 80, title, FontStyle::Bold, 26);
  drawTextCentered(SCREEN_W / 2, cy - 26, stage, FontStyle::Regular, 22);

  int16_t bx = (SCREEN_W - BAR_W) / 2;
  drawRect(bx, cy, BAR_W, BAR_H);
  if (total > 0) {
    int16_t filled = static_cast<int16_t>(
        (static_cast<int32_t>(BAR_W - 4) * done) / (total ? total : 1));
    fillRect(bx + 2, cy + 2, filled, BAR_H - 4, true);
  }

  char counter[32];
  snprintf(counter, sizeof(counter), "%u / %u", static_cast<unsigned>(done),
           static_cast<unsigned>(total));
  drawTextCentered(SCREEN_W / 2, cy + 50, counter, FontStyle::Regular, 20);
}

}  // namespace ui
