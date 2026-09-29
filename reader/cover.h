// Draws a book cover at any size, and a typographic stand-in when a book has
// no cover art.
//
// Cover bitmaps are 1 bit per pixel, 1 = ink (black), MSB first, each row
// padded to whole bytes: the layout Adafruit_GFX::drawBitmap expects.
#pragma once
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

#include "books.h"

static const int16_t COVER_W = 204;
static const int16_t COVER_H = 306;
static const int16_t COVER_ROW_BYTES = (COVER_W + 7) / 8;  // 26

// Thumbnail size used by the list view. Same 2:3 ratio as COVER_W x COVER_H,
// so downscaling stays undistorted.
static const int16_t THUMB_W = 72;
static const int16_t THUMB_H = 108;

// Legibility floor for the typographic placeholder's title/author text.
// Below this width the text would be packed-in, unreadable noise (this is
// what keeps drawCover's placeholder from writing over itself at THUMB_W),
// so drawCover draws the frame only and leaves the caption to the caller.
static const int16_t COVER_TEXT_MIN_W = 120;

// True if the source pixel is ink.
bool coverBit(const uint8_t *cover, int16_t sx, int16_t sy);

// Nearest-neighbour: which source pixel a destination pixel samples.
int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize);

// Copies `text` into `out` (an `outSize`-byte buffer), shortening it with a
// trailing "..." until it renders no wider than `maxW` pixels in `font`.
// Sets the font on `gfx` (needed to measure). Returns `out`.
const char *truncateToWidth(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                            int16_t maxW, char *out, size_t outSize);

// Draws `text` centred in a box `boxW` wide starting at `boxX`, with its
// baseline at `baseline`, truncating with "..." if it does not fit.
// Sets the font; the caller sets the colour.
void drawCentredText(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                     int16_t boxX, int16_t boxW, int16_t baseline);

// Draws `book`'s cover into the w x h box at (x, y), scaling as needed, with
// a 1px frame. Falls back to title and author centred in the frame when the
// book has no cover -- but only when w >= COVER_TEXT_MIN_W; below that the
// frame is drawn alone, since the text would not be legible and (in the list
// view) the caption is drawn beside the thumbnail instead.
void drawCover(Adafruit_GFX &gfx, const Book &book, int16_t x, int16_t y,
               int16_t w, int16_t h);
