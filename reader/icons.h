// Small line icons shared by the screens, each drawn centred on (cx, cy) in
// black. All about 20 px across, to sit beside 9-12pt text.
#pragma once
#include <Adafruit_GFX.h>

// A thin chevron pointing left ("<", back) or right (">", forward).
void iconChevronLeft(Adafruit_GFX &gfx, int16_t cx, int16_t cy);
void iconChevronRight(Adafruit_GFX &gfx, int16_t cx, int16_t cy);

// A small gear: settings.
void iconGear(Adafruit_GFX &gfx, int16_t cx, int16_t cy);

// Four squares: switch to the grid view.
void iconGrid(Adafruit_GFX &gfx, int16_t cx, int16_t cy);

// Three rows, each a dot and a line: switch to the list view.
void iconList(Adafruit_GFX &gfx, int16_t cx, int16_t cy);
