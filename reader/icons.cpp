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
