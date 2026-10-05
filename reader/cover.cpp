#include "cover.h"
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "storage.h"
#include "tjpgd.h"

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize) {
  return (int16_t)(((int32_t)dst * srcSize) / dstSize);
}

const char *truncateToWidth(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                            int16_t maxW, char *out, size_t outSize) {
  strncpy(out, text, outSize - 1);
  out[outSize - 1] = '\0';

  gfx.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(out, 0, 0, &x1, &y1, &w, &h);
  while (w > maxW && strlen(out) > 4) {
    size_t n = strlen(out);
    strcpy(out + n - 4, "...");
    gfx.getTextBounds(out, 0, 0, &x1, &y1, &w, &h);
  }
  return out;
}

void drawCentredText(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                     int16_t boxX, int16_t boxW, int16_t baseline) {
  char buf[48];
  truncateToWidth(gfx, text, font, boxW, buf, sizeof(buf));

  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(boxX + (boxW - (int16_t)w) / 2 - x1, baseline);
  gfx.print(buf);
}

// ---- JPEG covers ----

// Everything the decoder's callbacks need: where the JPEG bytes are, and where
// the decoded pixels go.
struct CoverDecode {
  ByteReader *in;

  uint8_t *gray;  // w x h output, one byte per pixel, 0 = black
  int16_t w, h;
  // The part of the (scaled) decoded image that fills the box.
  int32_t cropX, cropY, cropW, cropH;
};

// Feeds the decoder the next `n` bytes, or skips them when `buf` is null.
static size_t jpegInput(JDEC *jd, uint8_t *buf, size_t n) {
  CoverDecode *d = (CoverDecode *)jd->device;
  uint8_t scratch[64];
  size_t done = 0;
  while (done < n) {
    uint8_t *to = buf ? buf + done : scratch;
    uint32_t want = (uint32_t)(buf ? n - done : (n - done < sizeof(scratch) ? n - done : sizeof(scratch)));
    int32_t got = d->in->read(to, want);
    if (got <= 0) break;
    done += (size_t)got;
  }
  return done;
}

// Receives one decoded block of grayscale pixels. Each output pixel takes the
// value of the decoded pixel it maps to (nearest neighbour): walk the output
// pixels whose source falls inside this block.
static int jpegOutput(JDEC *jd, void *bitmap, JRECT *rect) {
  CoverDecode *d = (CoverDecode *)jd->device;
  const uint8_t *pixels = (const uint8_t *)bitmap;
  int32_t blockW = rect->right - rect->left + 1;

  int32_t dy = (rect->top - d->cropY) * d->h / d->cropH;
  if (dy < 0) dy = 0;
  for (; dy < d->h; dy++) {
    int32_t sy = d->cropY + coverSrcIndex(dy, d->h, d->cropH);
    if (sy < rect->top) continue;
    if (sy > rect->bottom) break;

    int32_t dx = (rect->left - d->cropX) * d->w / d->cropW;
    if (dx < 0) dx = 0;
    for (; dx < d->w; dx++) {
      int32_t sx = d->cropX + coverSrcIndex(dx, d->w, d->cropW);
      if (sx < rect->left) continue;
      if (sx > rect->right) break;
      d->gray[dy * d->w + dx] = pixels[(sy - rect->top) * blockW + (sx - rect->left)];
    }
  }
  return 1;  // continue decoding
}

// Stretches the levels so the darkest 1% of pixels become black and the
// brightest 1% white. Scanned covers are often washed out, which dithers to a
// flat grey.
static void stretchContrast(uint8_t *gray, int32_t count) {
  uint32_t histogram[256] = { 0 };
  for (int32_t i = 0; i < count; i++) histogram[gray[i]]++;
  int32_t clip = count / 100, seen = 0, lo = 0, hi = 255;
  while (lo < 255 && (seen += histogram[lo]) <= clip) lo++;
  seen = 0;
  while (hi > 0 && (seen += histogram[hi]) <= clip) hi--;
  if (hi - lo < 32) return;  // nearly flat: stretching would only amplify noise
  for (int32_t i = 0; i < count; i++) {
    int32_t v = (gray[i] - lo) * 255 / (hi - lo);
    gray[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
  }
}

// Floyd-Steinberg dithering: each pixel becomes black or white, and the
// rounding error is passed on to the neighbours not yet visited, so areas of
// grey come out as patterns of dots with the right average darkness.
// Writes 1 bit per pixel into `bits` (1 = ink, MSB first, rows padded to whole
// bytes). Returns false if it runs out of memory.
static bool ditherToBits(const uint8_t *gray, int16_t w, int16_t h, uint8_t *bits) {
  // Error carried into the current and next row, with a spare slot each side.
  int16_t *errors = (int16_t *)calloc(2 * (w + 2), sizeof(int16_t));
  if (!errors) return false;
  int16_t *cur = errors + 1, *next = errors + (w + 2) + 1;
  const int16_t rowBytes = (w + 7) / 8;
  memset(bits, 0, (size_t)rowBytes * h);

  for (int16_t py = 0; py < h; py++) {
    for (int16_t px = 0; px < w; px++) {
      int16_t v = gray[py * w + px] + cur[px];
      int16_t out = v < 128 ? 0 : 255;
      if (out == 0) bits[py * rowBytes + px / 8] |= 0x80 >> (px % 8);
      int16_t err = v - out;
      cur[px + 1] += err * 7 / 16;
      next[px - 1] += err * 3 / 16;
      next[px] += err * 5 / 16;
      next[px + 1] += err / 16;
    }
    int16_t *swap = cur;
    cur = next;
    next = swap;
    memset(next - 1, 0, (w + 2) * sizeof(int16_t));
  }
  free(errors);
  return true;
}

// Decodes a JPEG into a w x h grayscale image (0 = black), contrast not yet
// stretched. With fit = false the image is scaled and cropped to fill w x h
// exactly; with fit = true it keeps its proportions and all of it, and w and
// h are reduced to the size it fits in. Returns the image (malloc'd; the
// caller frees it), or nullptr (out of memory, or a JPEG the decoder does
// not handle, such as a progressive one).
static uint8_t *decodeGray(ByteReader &in, int16_t &w, int16_t &h, bool fit) {
  unsigned long t0 = millis();
  CoverDecode d = { &in, nullptr, w, h, 0, 0, 0, 0 };
  void *work = malloc(TJPGD_WORKSPACE_SIZE);
  JDEC jd;
  JRESULT res = JDR_MEM1;
  bool ok = false;

  if (work && (res = jd_prepare(&jd, jpegInput, work, TJPGD_WORKSPACE_SIZE, &d)) == JDR_OK) {
    int32_t cropW = jd.width, cropH = jd.height;
    if (fit) {  // shrink the box to the image's proportions
      if ((int32_t)jd.width * h > (int32_t)jd.height * w) h = (int16_t)((int32_t)jd.height * w / jd.width);
      else w = (int16_t)((int32_t)jd.width * h / jd.height);
      d.w = w;
      d.h = h;
    } else if ((int32_t)jd.width * h > (int32_t)jd.height * w) {
      cropW = (int32_t)jd.height * w / h;  // crop to the box's proportions, centred
    } else {
      cropH = (int32_t)jd.width * h / w;
    }

    // Let the decoder shrink the image by 1/2, 1/4 or 1/8 as far as it can
    // while staying at least as large as the box: much faster than decoding
    // a 2000px cover at full size, and its averaging makes a smoother result.
    uint8_t scale = 0;
    while (scale < 3 && (cropW >> (scale + 1)) >= w && (cropH >> (scale + 1)) >= h) scale++;

    d.cropW = cropW >> scale;
    d.cropH = cropH >> scale;
    d.cropX = ((jd.width >> scale) - d.cropW) / 2;
    d.cropY = ((jd.height >> scale) - d.cropH) / 2;
    d.gray = (uint8_t *)malloc((size_t)w * h);
    if (d.gray) {
      memset(d.gray, 255, (size_t)w * h);
      res = jd_decomp(&jd, jpegOutput, scale);
      ok = res == JDR_OK;
    } else {
      res = JDR_MEM1;
    }
    if (ok) Serial.printf("cover: %u x %u JPEG at 1/%d -> %d x %d in %lu ms\n", jd.width,
                          jd.height, 1 << scale, w, h, millis() - t0);
  }
  if (!ok) {
    Serial.printf("cover: could not be decoded (error %d)\n", (int)res);
    free(d.gray);
    d.gray = nullptr;
  }
  free(work);
  return d.gray;
}

// Shrinks a grayscale image by averaging the source pixels under each
// destination pixel, so the thumbnail keeps the cover's tones rather than a
// sampled speckle of them.
static void boxShrink(const uint8_t *src, int16_t sw, int16_t sh, uint8_t *dst, int16_t dw, int16_t dh) {
  for (int16_t dy = 0; dy < dh; dy++) {
    int16_t y0 = coverSrcIndex(dy, dh, sh), y1 = coverSrcIndex(dy + 1, dh, sh);
    if (y1 <= y0) y1 = y0 + 1;
    for (int16_t dx = 0; dx < dw; dx++) {
      int16_t x0 = coverSrcIndex(dx, dw, sw), x1 = coverSrcIndex(dx + 1, dw, sw);
      if (x1 <= x0) x1 = x0 + 1;
      uint32_t sum = 0, n = 0;
      for (int16_t y = y0; y < y1 && y < sh; y++)
        for (int16_t x = x0; x < x1 && x < sw; x++, n++) sum += src[y * sw + x];
      dst[dy * dw + dx] = (uint8_t)(n ? sum / n : 255);
    }
  }
}

bool coverRender(ByteReader &jpeg, uint8_t *gridBits, uint8_t *thumbBits) {
  int16_t w = COVER_W, h = COVER_H;
  uint8_t *gray = decodeGray(jpeg, w, h, false);
  if (!gray) return false;
  stretchContrast(gray, (int32_t)COVER_W * COVER_H);
  bool ok = ditherToBits(gray, COVER_W, COVER_H, gridBits);

  uint8_t *thumb = ok ? (uint8_t *)malloc((size_t)THUMB_W * THUMB_H) : nullptr;
  if (thumb) {
    boxShrink(gray, COVER_W, COVER_H, thumb, THUMB_W, THUMB_H);
    stretchContrast(thumb, (int32_t)THUMB_W * THUMB_H);
    ok = ditherToBits(thumb, THUMB_W, THUMB_H, thumbBits);
  } else {
    ok = false;
  }
  free(thumb);
  free(gray);
  return ok;
}

// The cover page: the cover fills a box with the screen's own proportions,
// inset by this margin at the sides (5% of the screen's width) and centred,
// so there is white all round it.
static const int16_t COVER_PAGE_MARGIN_PERCENT = 5;

// Full-screen covers can't be decoded at full size: a 480 x 800 grayscale
// image is 384 KB, more than the ESP32 has. So the cover is decoded at this
// size (~54 KB, the screen's proportions), then scaled up smoothly as it is
// dithered.
static const int16_t FULL_DECODE_W = 180, FULL_DECODE_H = 300;

bool coverRenderFull(ByteReader &jpeg, Adafruit_GFX &gfx) {
  const int16_t W = gfx.width(), H = gfx.height();
  int16_t w = FULL_DECODE_W, h = FULL_DECODE_H;
  if (W > H) w = FULL_DECODE_H, h = FULL_DECODE_W;
  uint8_t *gray = decodeGray(jpeg, w, h, false);  // cropped to fill w x h
  if (!gray) return false;
  stretchContrast(gray, (int32_t)w * h);

  // The box: the screen's proportions, inset, centred.
  const int16_t margin = (W < H ? W : H) * COVER_PAGE_MARGIN_PERCENT / 100;
  const int16_t tw = W - 2 * margin, th = (int16_t)((int32_t)tw * H / W);
  const int16_t ox = (W - tw) / 2, oy = (H - th) / 2;
  gfx.fillScreen(PAPER);

  // Floyd-Steinberg as in ditherToBits, reading each output pixel from the
  // small image by bilinear interpolation (8.8 fixed point).
  int16_t *errors = (int16_t *)calloc(2 * (tw + 2), sizeof(int16_t));
  if (!errors) {
    free(gray);
    return false;
  }
  int16_t *cur = errors + 1, *next = errors + (tw + 2) + 1;
  for (int16_t py = 0; py < th; py++) {
    int32_t sy = ((2 * py + 1) * (int32_t)h * 128) / th - 128;
    if (sy < 0) sy = 0;
    int16_t y0 = (int16_t)(sy >> 8), y1 = y0 + 1 < h ? y0 + 1 : y0;
    int32_t fy = sy & 255;
    for (int16_t px = 0; px < tw; px++) {
      int32_t sx = ((2 * px + 1) * (int32_t)w * 128) / tw - 128;
      if (sx < 0) sx = 0;
      int16_t x0 = (int16_t)(sx >> 8), x1 = x0 + 1 < w ? x0 + 1 : x0;
      int32_t fx = sx & 255;
      int32_t top = gray[y0 * w + x0] * (256 - fx) + gray[y0 * w + x1] * fx;
      int32_t bot = gray[y1 * w + x0] * (256 - fx) + gray[y1 * w + x1] * fx;
      int16_t v = (int16_t)((top * (256 - fy) + bot * fy) >> 16) + cur[px];
      int16_t out = v < 128 ? 0 : 255;
      if (out == 0) gfx.drawPixel(ox + px, oy + py, INK);
      int16_t err = v - out;
      cur[px + 1] += err * 7 / 16;
      next[px - 1] += err * 3 / 16;
      next[px] += err * 5 / 16;
      next[px + 1] += err / 16;
    }
    int16_t *swap = cur;
    cur = next;
    next = swap;
    memset(next - 1, 0, (tw + 2) * sizeof(int16_t));
  }
  free(errors);
  free(gray);
  gfx.drawRect(ox - 1, oy - 1, tw + 2, th + 2, INK);  // so a white cover still reads as a cover
  return true;
}

// Loads one of the two dithered images from the book's cover.bin, or
// nullptr. Which one: the thumbnail for small boxes, else the full size.
static uint8_t *loadCoverBits(const BookInfo &book, bool thumb) {
  char path[48];
  catalogPath(book, "cover.bin", path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return nullptr;
  uint32_t size = thumb ? COVER_THUMB_BYTES : COVER_GRID_BYTES;
  uint8_t *bits = (uint8_t *)malloc(size);
  bool ok = bits && storageSeek(f, thumb ? COVER_GRID_BYTES : 0) &&
            storageRead(f, bits, size) == (int32_t)size;
  storageClose(f);
  if (!ok) {
    free(bits);
    return nullptr;
  }
  return bits;
}

void drawCover(Adafruit_GFX &gfx, const BookInfo &book, int16_t x, int16_t y,
               int16_t w, int16_t h) {
  gfx.fillRect(x, y, w, h, PAPER);

  bool thumb = w <= THUMB_W;
  uint8_t *art = book.hasCover ? loadCoverBits(book, thumb) : nullptr;
  if (art) {
    int16_t sw = thumb ? THUMB_W : COVER_W, sh = thumb ? THUMB_H : COVER_H;
    if (w == sw && h == sh) {
      gfx.drawBitmap(x, y, art, w, h, INK);
    } else {  // any other size: nearest-neighbour from the closer one
      int16_t rowBytes = (sw + 7) / 8;
      for (int16_t py = 0; py < h; py++) {
        int16_t sy = coverSrcIndex(py, h, sh);
        for (int16_t px = 0; px < w; px++) {
          int16_t sx = coverSrcIndex(px, w, sw);
          if (art[sy * rowBytes + sx / 8] & (0x80 >> (sx % 8))) gfx.drawPixel(x + px, y + py, INK);
        }
      }
    }
    free(art);
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
