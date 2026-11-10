#include "icons.h"

static const uint16_t INK = 0x0000, PAPER = 0xFFFF;

void iconChevronLeft(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  for (int16_t t = 0; t < 2; t++) {  // 2 px thick
    gfx.drawLine(cx + 4 + t, cy - 8, cx - 4 + t, cy, INK);
    gfx.drawLine(cx - 4 + t, cy, cx + 4 + t, cy + 8, INK);
  }
}

void iconChevronRight(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  for (int16_t t = 0; t < 2; t++) {
    gfx.drawLine(cx - 4 - t, cy - 8, cx + 4 - t, cy, INK);
    gfx.drawLine(cx + 4 - t, cy, cx - 4 - t, cy + 8, INK);
  }
}

void iconGear(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  static const int8_t TEETH[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
                                      { 0, 1 },  { -1, 1 }, { -1, 0 }, { -1, -1 } };
  for (const auto &t : TEETH) {
    int16_t r = t[0] && t[1] ? 6 : 8;  // diagonal teeth reach the same distance
    gfx.fillRect(cx + t[0] * r - 1, cy + t[1] * r - 1, 3, 3, INK);
  }
  gfx.fillCircle(cx, cy, 6, INK);
  gfx.fillCircle(cx, cy, 3, PAPER);
}

void iconGrid(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  for (int16_t row = 0; row < 2; row++)
    for (int16_t col = 0; col < 2; col++) gfx.fillRect(cx - 9 + col * 11, cy - 9 + row * 11, 8, 8, INK);
}

void iconList(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  for (int16_t row = 0; row < 3; row++) {
    int16_t y = cy - 8 + row * 7;
    gfx.fillRect(cx - 10, y, 3, 3, INK);       // the bullet
    gfx.fillRect(cx - 4, y, 14, 3, INK);       // the line
  }
}

void iconSun(Adafruit_GFX &gfx, int16_t cx, int16_t cy, int16_t rays) {
  // The disc keeps a constant relation to the rays, so the small sun is the
  // large one scaled rather than a different drawing.
  int16_t r = rays / 2;
  gfx.fillCircle(cx, cy, r, INK);
  // Eight rays, the diagonals pulled in so every ray ends the same distance
  // from the centre.
  static const int8_t DIRS[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
                                     { 0, 1 },  { -1, 1 }, { -1, 0 }, { -1, -1 } };
  for (const auto &d : DIRS) {
    bool diagonal = d[0] && d[1];
    int16_t inner = diagonal ? (r + 2) * 7 / 10 : r + 2;
    int16_t outer = diagonal ? rays * 7 / 10 : rays;
    gfx.drawLine(cx + d[0] * inner, cy + d[1] * inner,
                 cx + d[0] * outer, cy + d[1] * outer, INK);
  }
}

void iconMoon(Adafruit_GFX &gfx, int16_t cx, int16_t cy) {
  // A filled disc with a second disc punched out of it, offset up and right:
  // the crescent is what is left.
  gfx.fillCircle(cx, cy, 9, INK);
  gfx.fillCircle(cx + 4, cy - 3, 8, PAPER);
}
