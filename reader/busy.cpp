#include "busy.h"

#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>

#include "cover.h"
#include "epd.h"
#include "screens.h"

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;
static const int16_t BAR_W = 280;

void busyShow(const char *title, const char *message, int percent) {
  GFXcanvas1 &gfx = appCanvas();
  const int16_t w = gfx.width(), mid = gfx.height() / 2;
  gfx.fillScreen(PAPER);
  gfx.setTextWrap(false);
  gfx.setTextColor(INK);
  drawCentredText(gfx, title, &FreeSerifBold12pt7b, 24, w - 48, mid - 30);
  drawCentredText(gfx, message, &FreeSerif9pt7b, 24, w - 48, mid + 6);
  if (percent >= 0) {  // like a book's progress line: a thin track, a thicker part done
    int16_t x = (w - BAR_W) / 2, y = mid + 32;
    gfx.drawFastHLine(x, y, BAR_W, INK);
    int16_t filled = (int16_t)(BAR_W * (percent > 100 ? 100 : percent) / 100);
    if (filled > 0) gfx.fillRect(x, y - 1, filled, 3, INK);
  }
  appRefresh(false);  // full anyway if it is the first screen since power-up
}
