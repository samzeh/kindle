#include "text/FontCache.h"

#include "config.h"
#include "stb_truetype.h"
#include "text/fonts/CharisSubset.h"

namespace {

// Raster cache budget. Sized so a full page of English text fits without
// evicting: ~80 distinct glyphs in the body style plus emphasis runs.
constexpr size_t ARENA_BYTES = 24 * 1024;
constexpr uint8_t WAYS = 4;

// Coverage above this alpha becomes ink. Slightly below mid-grey so thin
// serifs survive the 1-bit threshold.
constexpr uint8_t INK_THRESHOLD = 110;

// Scratch for stb_truetype's 8bpp output before it is packed to 1bpp. Sized
// for the largest glyph at the largest selectable size.
constexpr int SCRATCH_W = 80;
constexpr int SCRATCH_H = 96;

// Heap rather than .bss: the ESP32's static DRAM segment is only ~176KB and
// the 48KB framebuffer already lives there, but the heap also draws on DRAM
// regions the linker cannot place static data in.
uint8_t* gArena = nullptr;
uint8_t* gScratch = nullptr;

stbtt_fontinfo gInfo[FONT_STYLE_COUNT];

const uint8_t* faceData(uint8_t style) {
  switch (style) {
    case 1: return kCharisBold;
    case 2: return kCharisItalic;
    case 3: return kCharisBoldItalic;
    default: return kCharisRegular;
  }
}

}  // namespace

FontCache gFonts;

FontCache::Slot* FontCache::slots() {
  return reinterpret_cast<Slot*>(gArena);
}

bool FontCache::begin() {
  if (!gArena) gArena = static_cast<uint8_t*>(malloc(ARENA_BYTES));
  if (!gScratch) gScratch = static_cast<uint8_t*>(malloc(SCRATCH_W * SCRATCH_H));
  if (!gArena || !gScratch) {
    log_e("fonts: could not allocate %u bytes for the glyph cache",
          static_cast<unsigned>(ARENA_BYTES + SCRATCH_W * SCRATCH_H));
    return false;
  }

  for (uint8_t i = 0; i < FONT_STYLE_COUNT; i++) {
    const uint8_t* data = faceData(i);
    if (!stbtt_InitFont(&gInfo[i], data, stbtt_GetFontOffsetForIndex(data, 0))) {
      log_e("stbtt_InitFont failed for style %u", i);
      return false;
    }
  }
  ready_ = true;
  setSize(FONT_SIZES[FONT_SIZE_DEFAULT]);
  return true;
}

int16_t FontCache::lineHeightFor(uint16_t px) const {
  if (!ready_) return px;
  // Em-relative scaling, so a "26px font" means a 26px em the way CSS does.
  // stbtt_ScaleForPixelHeight would instead fit ascent-descent into 26px,
  // which renders visibly smaller than the number suggests.
  float scale = stbtt_ScaleForMappingEmToPixels(&gInfo[0], px);
  int a, d, g;
  stbtt_GetFontVMetrics(&gInfo[0], &a, &d, &g);
  return static_cast<int16_t>((a - d + g) * scale + 0.5f);
}

void FontCache::setSize(uint16_t px) {
  if (!ready_ || px == px_) return;
  px_ = px;

  for (uint8_t s = 0; s < FONT_STYLE_COUNT; s++) {
    scale_[s] = stbtt_ScaleForMappingEmToPixels(&gInfo[s], px);
  }

  int a, d, g;
  stbtt_GetFontVMetrics(&gInfo[0], &a, &d, &g);
  ascent_ = static_cast<int16_t>(a * scale_[0] + 0.5f);
  descent_ = static_cast<int16_t>(d * scale_[0] - 0.5f);
  lineHeight_ = static_cast<int16_t>((a - d + g) * scale_[0] + 0.5f);

  for (uint8_t s = 0; s < FONT_STYLE_COUNT; s++) {
    for (uint32_t cp = ASCII_LO; cp < ASCII_HI; cp++) {
      asciiAdvance_[s][cp - ASCII_LO] = measure(static_cast<FontStyle>(s), cp);
    }
  }

  // Size the slab for the widest plausible glyph at this size, then carve the
  // arena into as many slots as will fit.
  uint16_t maxW = static_cast<uint16_t>(px * 1.6f) + 2;
  uint16_t maxH = static_cast<uint16_t>(px * 1.6f) + 2;
  slotBytes_ = ((maxW + 7) / 8) * maxH;

  size_t perSlot = slotBytes_ + sizeof(Slot);
  slotCount_ = static_cast<uint16_t>(ARENA_BYTES / perSlot);
  slotCount_ -= slotCount_ % WAYS;  // keep whole sets

  flush();
}

void FontCache::flush() {
  if (!gArena) return;
  Slot* table = slots();
  for (uint16_t i = 0; i < slotCount_; i++) {
    table[i].key = 0;
    table[i].stamp = 0;
  }
  stamp_ = 0;
}

int16_t FontCache::measure(FontStyle style, uint32_t codepoint) const {
  uint8_t s = static_cast<uint8_t>(style);
  int advance = 0, lsb = 0;
  stbtt_GetCodepointHMetrics(&gInfo[s], static_cast<int>(codepoint), &advance, &lsb);
  return static_cast<int16_t>(advance * scale_[s] + 0.5f);
}

int16_t FontCache::advance(FontStyle style, uint32_t codepoint) const {
  if (!ready_) return 0;
  uint8_t s = static_cast<uint8_t>(style);
  if (codepoint >= ASCII_LO && codepoint < ASCII_HI) {
    return asciiAdvance_[s][codepoint - ASCII_LO];
  }
  return measure(style, codepoint);
}

bool FontCache::rasterise(FontStyle style, uint32_t codepoint, Slot& out, uint8_t* dest) {
  uint8_t s = static_cast<uint8_t>(style);
  int index = stbtt_FindGlyphIndex(&gInfo[s], static_cast<int>(codepoint));
  if (index == 0) return false;

  int x0, y0, x1, y1;
  stbtt_GetGlyphBitmapBox(&gInfo[s], index, scale_[s], scale_[s], &x0, &y0, &x1, &y1);

  int w = x1 - x0;
  int h = y1 - y0;
  if (w < 0 || h < 0) return false;
  if (w > SCRATCH_W) w = SCRATCH_W;
  if (h > SCRATCH_H) h = SCRATCH_H;

  uint8_t stride = static_cast<uint8_t>((w + 7) / 8);
  if (static_cast<uint16_t>(stride) * h > slotBytes_) return false;

  memset(gScratch, 0, static_cast<size_t>(w) * h);
  stbtt_MakeGlyphBitmap(&gInfo[s], gScratch, w, h, w, scale_[s], scale_[s], index);

  // Pack the 8bpp coverage map down to 1bpp, MSB first.
  memset(dest, 0, static_cast<size_t>(stride) * h);
  for (int y = 0; y < h; y++) {
    const uint8_t* src = gScratch + static_cast<size_t>(y) * w;
    uint8_t* row = dest + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; x++) {
      if (src[x] >= INK_THRESHOLD) row[x >> 3] |= 0x80 >> (x & 7);
    }
  }

  int adv = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(&gInfo[s], index, &adv, &lsb);

  out.glyph.bits = dest;
  out.glyph.advance = static_cast<int16_t>(adv * scale_[s] + 0.5f);
  out.glyph.xoff = static_cast<int16_t>(x0);
  out.glyph.yoff = static_cast<int16_t>(y0);
  out.glyph.w = static_cast<uint8_t>(w);
  out.glyph.h = static_cast<uint8_t>(h);
  out.glyph.stride = stride;
  return true;
}

const Glyph* FontCache::glyph(FontStyle style, uint32_t codepoint) {
  if (!ready_ || slotCount_ == 0 || !gArena) return nullptr;

  uint32_t key = (static_cast<uint32_t>(style) << 24) | (codepoint & 0x00FFFFFF);
  if (key == 0) return nullptr;  // the empty marker; codepoint 0 is not drawable

  Slot* table = slots();
  uint8_t* payload = gArena + static_cast<size_t>(slotCount_) * sizeof(Slot);

  uint16_t sets = slotCount_ / WAYS;
  uint16_t set = static_cast<uint16_t>((key * 2654435761u) % sets);
  uint16_t base = set * WAYS;

  for (uint8_t w = 0; w < WAYS; w++) {
    Slot& slot = table[base + w];
    if (slot.key == key) {
      slot.stamp = ++stamp_;
      return &slot.glyph;
    }
  }

  // Miss: take a free way, else the least recently used one in this set.
  uint8_t victim = 0;
  uint32_t oldest = UINT32_MAX;
  for (uint8_t w = 0; w < WAYS; w++) {
    Slot& slot = table[base + w];
    if (slot.key == 0) {
      victim = w;
      break;
    }
    if (slot.stamp < oldest) {
      oldest = slot.stamp;
      victim = w;
    }
  }

  Slot& slot = table[base + victim];
  uint8_t* dest = payload + static_cast<size_t>(base + victim) * slotBytes_;

  if (!rasterise(style, codepoint, slot, dest)) {
    slot.key = 0;
    return nullptr;
  }

  slot.key = key;
  slot.stamp = ++stamp_;
  return &slot.glyph;
}
