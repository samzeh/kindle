#include "cover.h"
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <string.h>

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

bool coverBit(const uint8_t *cover, int16_t sx, int16_t sy) {
  return cover[sy * COVER_ROW_BYTES + sx / 8] & (0x80 >> (sx % 8));
}

int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize) {
  return (int16_t)(((int32_t)dst * srcSize) / dstSize);
}

void drawCentredText(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                     int16_t boxX, int16_t boxW, int16_t baseline) {
  char buf[48];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  gfx.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  while (w > boxW && strlen(buf) > 4) {
    size_t n = strlen(buf);
    strcpy(buf + n - 4, "...");
    gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  }
  gfx.setCursor(boxX + (boxW - (int16_t)w) / 2 - x1, baseline);
  gfx.print(buf);
}

void drawCover(Adafruit_GFX &gfx, const Book &book, int16_t x, int16_t y,
               int16_t w, int16_t h) {
  gfx.fillRect(x, y, w, h, PAPER);

  if (book.cover) {
    for (int16_t py = 0; py < h; py++) {
      int16_t sy = coverSrcIndex(py, h, COVER_H);
      for (int16_t px = 0; px < w; px++) {
        int16_t sx = coverSrcIndex(px, w, COVER_W);
        if (coverBit(book.cover, sx, sy)) gfx.drawPixel(x + px, y + py, INK);
      }
    }
  } else if (w >= COVER_TEXT_MIN_W) {
    // No cover art, and room enough to letter it: set the title and author
    // inside the frame instead. Below the legibility floor (e.g. a list-view
    // thumbnail) the frame is left empty; the caller draws the caption.
    gfx.setTextColor(INK);
    gfx.setTextWrap(false);
    int16_t inset = 10;
    drawCentredText(gfx, book.title, &FreeSerifBold9pt7b, x + inset,
                    w - 2 * inset, y + h / 2 - 6);
    drawCentredText(gfx, book.author, &FreeSerif9pt7b, x + inset,
                    w - 2 * inset, y + h / 2 + 18);
  }

  gfx.drawRect(x, y, w, h, INK);
}
