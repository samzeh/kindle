// Draws a book cover at any size, and a typographic stand-in when a book has
// no cover art.
//
// Covers are the JPEG images inside the books' EPUBs. When a book is first
// seen, coverRender decodes its cover once (grayscale, scaled and cropped to
// fit, then dithered to black and white) at both sizes the library uses, and
// the result is kept in the book's cache (cover.bin). drawCover then only
// copies those bits to the screen.
#pragma once
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>

#include "bytes.h"
#include "catalog.h"

static const int16_t COVER_W = 204;
static const int16_t COVER_H = 306;

// Thumbnail size used by the list view. Same 2:3 ratio as COVER_W x COVER_H,
// so downscaling stays undistorted.
static const int16_t THUMB_W = 72;
static const int16_t THUMB_H = 108;

// cover.bin: the full-size image, then the thumbnail, each 1 bit per pixel
// (1 = ink, MSB first, rows padded to whole bytes).
static const uint32_t COVER_GRID_BYTES = (uint32_t)((COVER_W + 7) / 8) * COVER_H;   // 7956
static const uint32_t COVER_THUMB_BYTES = (uint32_t)((THUMB_W + 7) / 8) * THUMB_H;  // 972

// Legibility floor for the typographic placeholder's title/author text.
// Below this width the text would be packed-in, unreadable noise (this is
// what keeps drawCover's placeholder from writing over itself at THUMB_W),
// so drawCover draws the frame only and leaves the caption to the caller.
static const int16_t COVER_TEXT_MIN_W = 120;

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

// Decodes a JPEG cover and dithers it at both sizes into gridBits
// (COVER_GRID_BYTES) and thumbBits (COVER_THUMB_BYTES). The image fills each
// box: it is scaled to cover it and the overflow is cropped equally from
// both sides. Takes 0.6 to 1.4 s on the ESP32 and briefly needs about 75 KB
// of heap. False if it cannot (out of memory, or a JPEG the decoder does not
// handle, such as a progressive one).
bool coverRender(ByteReader &jpeg, uint8_t *gridBits, uint8_t *thumbBits);

// Draws a cover as a whole page, for the first page of a book: filling a
// box with the screen's proportions (cropped to fit, like a phone
// wallpaper), inset with white all round and a thin frame. Decoded at a
// reduced size (about 54 KB of heap) and scaled up smoothly while
// dithering, since a full-size grayscale image would not fit in the ESP32's
// memory.
bool coverRenderFull(ByteReader &jpeg, Adafruit_GFX &gfx);

// Draws `book`'s cover into the w x h box at (x, y), with a 1px frame, from
// its cover.bin (exact at COVER_W x COVER_H and THUMB_W x THUMB_H).
//
// Falls back to title and author centred in the frame when the book has no
// cover -- but only when w >= COVER_TEXT_MIN_W; below that the frame is drawn
// alone, since the text would not be legible and (in the list view) the
// caption is drawn beside the thumbnail instead.
void drawCover(Adafruit_GFX &gfx, const BookInfo &book, int16_t x, int16_t y,
               int16_t w, int16_t h);
