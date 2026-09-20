// Host-side check of the font cache and line breaker.
//
// The critical property is that the measuring pass (renderer == nullptr,
// used to build the page index) and the drawing pass agree on every page
// boundary. If they ever disagree, page numbers drift from what is on screen.

#include <cstdio>
#include <string>
#include <vector>

#include "Harness.h"
#include "text/FontCache.h"
#include "text/Layout.h"

namespace {

const char* kSample =
    "It is a truth universally acknowledged, that a single man in possession "
    "of a good fortune, must be in want of a wife.\n\n"
    "However little known the feelings or views of such a man may be on his "
    "first entering a neighbourhood, this truth is so well fixed in the minds "
    "of the surrounding families, that he is considered as the rightful "
    "property of some one or other of their daughters.\n\n"
    "\"My dear Mr. Bennet,\" said his lady to him one day, \"have you heard "
    "that Netherfield Park is let at last?\"\n\n"
    "Mr. Bennet replied that he had not.\n\n"
    "\"But it is,\" returned she; \"for Mrs. Long has just been here, and she "
    "told me all about it.\"\n\n"
    "Mr. Bennet made no answer.\n\n"
    "\"Do you not want to know who has taken it?\" cried his wife "
    "impatiently.\n\n"
    "\"You want to tell me, and I have no objection to hearing it.\"\n\n"
    "This was invitation enough. Naive resume cafe coordinate \xe2\x80\x94 an "
    "em dash, curly \xe2\x80\x9cquotes\xe2\x80\x9d and an ellipsis\xe2\x80\xa6 "
    "plus accented words like na\xc3\xafve and Bront\xc3\xab to exercise the "
    "Latin-1 range of the subset.\n\n";

int failures = 0;

void check(bool cond, const char* what) {
  if (!cond) {
    printf("  FAIL: %s\n", what);
    failures++;
  }
}

PageMetrics metrics(uint16_t fontPx) {
  PageMetrics m;
  m.x = MARGIN_CHOICES[MARGIN_DEFAULT];
  m.y = 16;
  m.w = SCREEN_W - 2 * MARGIN_CHOICES[MARGIN_DEFAULT];
  m.h = SCREEN_H - 16 - STATUS_BAR_H;
  m.fontPx = fontPx;
  m.lineSpacingPct = LINE_SPACING_DEFAULT;
  return m;
}

// Paginate the whole document, optionally drawing. Returns the page-start
// states so the two passes can be compared.
std::vector<SourceState> paginate(const std::string& text, const PageMetrics& m,
                                  BitmapRenderer* renderer,
                                  const char* pgmPrefix) {
  MemTextSource src(text);
  std::vector<SourceState> starts;

  SourceState cursor;
  for (int page = 0; page < 64; page++) {
    if (!src.restore(cursor)) break;
    starts.push_back(cursor);

    if (renderer) renderer->clear();
    PageResult res = Layout::layoutPage(src, m, renderer);

    if (renderer && pgmPrefix) {
      char path[256];
      snprintf(path, sizeof(path), "%s-%02d.pgm", pgmPrefix, page);
      renderer->writePgm(path);
    }

    if (res.endOfContent) break;
    check(res.next.offset > cursor.offset, "page advanced");
    if (res.next.offset <= cursor.offset) break;
    cursor = res.next;
  }
  return starts;
}

}  // namespace

int main(int argc, char** argv) {
  const char* outPrefix = argc > 1 ? argv[1] : "/tmp/page";

  if (!gFonts.begin()) {
    printf("FATAL: font init failed\n");
    return 1;
  }
  printf("font ready: size=%u lineHeight=%d ascent=%d descent=%d\n",
         gFonts.size(), gFonts.lineHeight(), gFonts.ascent(), gFonts.descent());

  // Sanity: metrics must be plausible for the default 26px body size.
  check(gFonts.lineHeight() > 26 && gFonts.lineHeight() < 50, "line height sane");
  check(gFonts.ascent() > 0 && gFonts.descent() < 0, "vertical metrics signed correctly");

  const Glyph* g = gFonts.glyph(FontStyle::Regular, 'A');
  check(g != nullptr, "'A' rasterises");
  if (g) {
    printf("glyph 'A': %ux%u stride=%u advance=%d xoff=%d yoff=%d\n", g->w, g->h,
           g->stride, g->advance, g->xoff, g->yoff);
    check(g->w > 5 && g->h > 5, "'A' has sensible extents");
    check(g->advance > 0, "'A' advances");
  }

  // Codepoints inside and outside the subset.
  check(gFonts.glyph(FontStyle::Italic, 0x2014) != nullptr, "em dash present");
  check(gFonts.glyph(FontStyle::Regular, 0x00EF) != nullptr, "i-diaeresis present");
  check(gFonts.glyph(FontStyle::Bold, 'Q') != nullptr, "bold face works");
  check(gFonts.glyph(FontStyle::Regular, 0x4E2D) == nullptr, "CJK absent from subset");

  std::string text;
  for (int i = 0; i < 3; i++) text += kSample;

  for (uint8_t si = 0; si < FONT_SIZE_COUNT; si++) {
    uint16_t px = FONT_SIZES[si];
    PageMetrics m = metrics(px);

    auto measured = paginate(text, m, nullptr, nullptr);

    BitmapRenderer renderer(SCREEN_W, SCREEN_H);
    auto drawn = paginate(text, m, &renderer,
                          px == FONT_SIZES[FONT_SIZE_DEFAULT] ? outPrefix : nullptr);

    printf("size %2upx: %zu pages measured, %zu drawn, %ld px clipped\n", px,
           measured.size(), drawn.size(), renderer.clipped_);

    check(measured.size() == drawn.size(), "measure and draw agree on page count");
    if (measured.size() == drawn.size()) {
      for (size_t i = 0; i < measured.size(); i++) {
        if (measured[i].offset != drawn[i].offset) {
          printf("  FAIL: page %zu starts at %u measured vs %u drawn\n", i,
                 measured[i].offset, drawn[i].offset);
          failures++;
          break;
        }
      }
    }
    check(renderer.clipped_ == 0, "nothing drawn outside the canvas");
    check(measured.size() >= 1, "produced at least one page");
  }

  // Larger text must need at least as many pages as smaller text.
  size_t small = paginate(text, metrics(FONT_SIZES[0]), nullptr, nullptr).size();
  size_t large = paginate(text, metrics(FONT_SIZES[FONT_SIZE_COUNT - 1]), nullptr,
                          nullptr)
                     .size();
  printf("pages: %zu at %upx vs %zu at %upx\n", small, FONT_SIZES[0], large,
         FONT_SIZES[FONT_SIZE_COUNT - 1]);
  check(large > small, "larger font paginates into more pages");

  printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
