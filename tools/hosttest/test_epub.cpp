// Host-side check of the streaming XHTML tokeniser and fragment source.
//
// Beyond "does it parse", the property that matters is that SourceState is a
// complete resume point: restoring to a page's saved state must reproduce
// that page exactly. If it does not, jumping to a page from the index shows
// something different from reading forward to it.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Harness.h"
#include "epub/ByteFeed.h"
#include "epub/XhtmlSource.h"
#include "epub/XmlPull.h"
#include "text/FontCache.h"
#include "text/Layout.h"

namespace {

int failures = 0;

void check(bool cond, const char* what) {
  if (!cond) {
    printf("  FAIL: %s\n", what);
    failures++;
  }
}

const char* kDoc = R"DOC(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>Should Not Appear</title>
  <style type="text/css">body { margin: 0; } .drop { font-size: 2em; }</style>
  <script>var hidden = "invisible";</script>
</head>
<body>
  <h1 class="chapter">Chapter One</h1>
  <h2>The Beginning &amp; The End</h2>
  <!-- a comment that must not render -->
  <p>Call me <em>Ishmael</em>. Some years ago&#8212;never mind how long
  precisely&#x2014;having little or no money in my purse, I thought I would
  sail about a little.</p>
  <p>It is a way I have of <strong>driving off the spleen</strong>, and
  <em>regulating the <strong>circulation</strong></em>.</p>
  <blockquote>Whenever I find myself growing grim about the mouth.</blockquote>
  <ul>
    <li>First item in the list</li>
    <li>Second item, rather longer than the first one so that it wraps</li>
  </ul>
  <p>A line<br/>broken in two.</p>
  <hr/>
  <p>Caf&eacute; na&#239;ve r&#xE9;sum&#xE9; &ldquo;quoted&rdquo; &hellip; done.</p>
  <p><![CDATA[Raw cdata content here.]]></p>
</body>
</html>)DOC";

std::string bigDoc() {
  std::string s = "<html><body>";
  for (int i = 0; i < 12; i++) {
    s += "<h2>Section ";
    s += std::to_string(i);
    s += "</h2>";
    s += "<p>It is a truth universally acknowledged, that a single man in "
         "possession of a good fortune, must be in want of a wife. However "
         "little known the feelings or views of such a man may be on his first "
         "entering a neighbourhood, this truth is so well fixed in the minds of "
         "the surrounding families, that he is considered as the "
         "<em>rightful property</em> of some one or other of their "
         "<strong>daughters</strong>.</p>";
  }
  s += "</body></html>";
  return s;
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

void testTokenizer() {
  printf("-- tokenizer --\n");

  MemoryFeed feed(reinterpret_cast<const uint8_t*>(kDoc), strlen(kDoc));
  XhtmlSource src;
  src.begin(&feed);

  std::string words;
  int paraBreaks = 0, lineBreaks = 0, rules = 0, bullets = 0;
  int italicWords = 0, boldWords = 0, h1Words = 0;

  Frag f;
  while (src.next(f)) {
    switch (f.kind) {
      case FragKind::Word:
        words.append(f.text, f.len);
        words.push_back(' ');
        if (strncmp(f.text, "\xe2\x80\xa2", 3) == 0 && f.len == 3) bullets++;
        if (f.emphasis & EMPH_ITALIC) italicWords++;
        if (f.emphasis & EMPH_BOLD) boldWords++;
        if (f.block == BlockStyle::H1) h1Words++;
        break;
      case FragKind::ParaBreak: paraBreaks++; break;
      case FragKind::LineBreak: lineBreaks++; break;
      case FragKind::Rule: rules++; break;
      default: break;
    }
  }

  check(words.find("Should Not Appear") == std::string::npos, "<title> skipped");
  check(words.find("margin") == std::string::npos, "<style> skipped");
  check(words.find("invisible") == std::string::npos, "<script> skipped");
  check(words.find("comment") == std::string::npos, "comment skipped");

  check(words.find("Ishmael") != std::string::npos, "body text present");
  check(words.find("Chapter One") != std::string::npos, "h1 text present");
  check(words.find("&") != std::string::npos, "&amp; decoded to '&'");
  check(words.find("\xe2\x80\x94") != std::string::npos, "&#8212; decoded to em dash");
  check(words.find("\xc3\xa9") != std::string::npos, "&eacute; decoded");
  check(words.find("\xc3\xaf") != std::string::npos, "&#239; decoded");
  check(words.find("\xe2\x80\x9c") != std::string::npos, "&ldquo; decoded");
  check(words.find("\xe2\x80\xa6") != std::string::npos, "&hellip; decoded");
  check(words.find("Raw cdata content") != std::string::npos, "CDATA emitted");

  check(h1Words == 2, "h1 words tagged as H1");
  check(italicWords >= 3, "italic emphasis tracked");
  check(boldWords >= 4, "bold emphasis tracked");
  check(bullets == 2, "one bullet per list item");
  check(lineBreaks == 1, "<br/> produced a line break");
  check(rules == 1, "<hr/> produced a rule");
  check(paraBreaks >= 8, "block ends produced paragraph breaks");

  printf("   words=%zu paraBreaks=%d bullets=%d italic=%d bold=%d\n",
         words.size(), paraBreaks, bullets, italicWords, boldWords);
}

void testNestedEmphasis() {
  printf("-- nested emphasis --\n");
  const char* doc =
      "<p>plain <em>ital <strong>both</strong> ital</em> plain</p>";
  MemoryFeed feed(reinterpret_cast<const uint8_t*>(doc), strlen(doc));
  XhtmlSource src;
  src.begin(&feed);

  Frag f;
  std::vector<std::pair<std::string, uint8_t>> got;
  while (src.next(f)) {
    if (f.kind == FragKind::Word) got.emplace_back(std::string(f.text, f.len), f.emphasis);
  }

  check(got.size() == 5, "five words");
  if (got.size() == 5) {
    check(got[0].second == 0, "'plain' has no emphasis");
    check(got[1].second == EMPH_ITALIC, "'ital' is italic");
    check(got[2].second == (EMPH_ITALIC | EMPH_BOLD), "'both' is bold+italic");
    check(got[3].second == EMPH_ITALIC, "emphasis pops back to italic");
    check(got[4].second == 0, "emphasis fully cleared");
  }
}

void testWordJoining() {
  printf("-- word joining --\n");
  // Punctuation after an inline tag is its own text node. Without join
  // tracking the layout would render "daughters ." and "so ,".
  const char* doc =
      "<p>their <strong>daughters</strong>. And <em>so</em>, at "
      "<em>last</em></p>";
  MemoryFeed feed(reinterpret_cast<const uint8_t*>(doc), strlen(doc));
  XhtmlSource src;
  src.begin(&feed);

  std::vector<std::pair<std::string, bool>> got;
  Frag f;
  while (src.next(f)) {
    if (f.kind == FragKind::Word) got.emplace_back(std::string(f.text, f.len), f.joinPrev);
  }

  const std::pair<const char*, bool> want[] = {
      {"their", false}, {"daughters", false}, {".", true},    {"And", false},
      {"so", false},    {",", true},          {"at", false},  {"last", false},
  };

  check(got.size() == 8, "eight words");
  for (size_t i = 0; i < got.size() && i < 8; i++) {
    if (got[i].first != want[i].first || got[i].second != want[i].second) {
      printf("  FAIL: word %zu is '%s' join=%d, wanted '%s' join=%d\n", i,
             got[i].first.c_str(), got[i].second, want[i].first, want[i].second);
      failures++;
    }
  }
}

void testNonBreakingSpace() {
  printf("-- non-breaking space --\n");
  // &#160; decodes to UTF-8 C2 A0. Matching only the A0 byte would split the
  // sequence and leave an orphan C2 glued to the previous word.
  const char* doc = "<p>Mr.&#160;Bennet and Mrs.&nbsp;Long</p>";
  MemoryFeed feed(reinterpret_cast<const uint8_t*>(doc), strlen(doc));
  XhtmlSource src;
  src.begin(&feed);

  std::vector<std::string> got;
  Frag f;
  while (src.next(f)) {
    if (f.kind == FragKind::Word) got.emplace_back(f.text, f.len);
  }

  const char* want[] = {"Mr.", "Bennet", "and", "Mrs.", "Long"};
  check(got.size() == 5, "NBSP separates words");
  for (size_t i = 0; i < got.size() && i < 5; i++) {
    if (got[i] != want[i]) {
      printf("  FAIL: word %zu is '%s' (%zu bytes), wanted '%s'\n", i,
             got[i].c_str(), got[i].size(), want[i]);
      failures++;
    }
  }
}

// Paginate, capturing each page's start state and a hash of its pixels.
std::vector<SourceState> paginate(const std::string& doc, const PageMetrics& m,
                                  std::vector<uint32_t>* hashes,
                                  const char* pgmPrefix) {
  MemoryFeed feed(reinterpret_cast<const uint8_t*>(doc.data()), doc.size());
  XhtmlSource src;
  src.begin(&feed);

  BitmapRenderer renderer(SCREEN_W, SCREEN_H);
  std::vector<SourceState> starts;
  SourceState cursor;

  for (int page = 0; page < 128; page++) {
    if (!src.restore(cursor)) break;
    starts.push_back(cursor);

    renderer.clear();
    PageResult res = Layout::layoutPage(src, m, hashes ? &renderer : nullptr);

    if (hashes) hashes->push_back(static_cast<uint32_t>(renderer.inked()));
    if (pgmPrefix) {
      char path[256];
      snprintf(path, sizeof(path), "%s-%02d.pgm", pgmPrefix, page);
      renderer.writePgm(path);
    }

    if (res.endOfContent) break;
    if (res.next.offset < cursor.offset ||
        (res.next.offset == cursor.offset && res.next.sub <= cursor.sub)) {
      printf("  FAIL: page %d did not advance (%u/%u -> %u/%u)\n", page,
             cursor.offset, cursor.sub, res.next.offset, res.next.sub);
      failures++;
      break;
    }
    cursor = res.next;
  }
  return starts;
}

void testPagination(const char* outPrefix) {
  printf("-- pagination --\n");
  std::string doc = bigDoc();

  for (uint8_t si = 0; si < FONT_SIZE_COUNT; si++) {
    PageMetrics m = metrics(FONT_SIZES[si]);

    auto measured = paginate(doc, m, nullptr, nullptr);
    std::vector<uint32_t> hashes;
    auto drawn = paginate(doc, m, &hashes,
                          FONT_SIZES[si] == FONT_SIZES[FONT_SIZE_DEFAULT] ? outPrefix
                                                                         : nullptr);

    printf("   %2upx -> %zu pages\n", FONT_SIZES[si], drawn.size());
    check(measured.size() == drawn.size(), "measure and draw agree on page count");

    size_t n = measured.size() < drawn.size() ? measured.size() : drawn.size();
    for (size_t i = 0; i < n; i++) {
      if (measured[i].offset != drawn[i].offset || measured[i].sub != drawn[i].sub) {
        printf("  FAIL: page %zu boundary differs\n", i);
        failures++;
        break;
      }
    }

    // Random access: restoring to a saved page state must redraw the very
    // same pixels the sequential pass produced.
    MemoryFeed feed(reinterpret_cast<const uint8_t*>(doc.data()), doc.size());
    XhtmlSource src;
    src.begin(&feed);
    BitmapRenderer renderer(SCREEN_W, SCREEN_H);

    for (size_t i = 0; i < drawn.size(); i++) {
      renderer.clear();
      check(src.restore(drawn[i]), "restore to page state");
      Layout::layoutPage(src, m, &renderer);
      if (static_cast<uint32_t>(renderer.inked()) != hashes[i]) {
        printf("  FAIL: page %zu differs after restore (%ld vs %u ink)\n", i,
               renderer.inked(), hashes[i]);
        failures++;
        break;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* outPrefix = argc > 1 ? argv[1] : "/tmp/epub";

  if (!gFonts.begin()) {
    printf("FATAL: font init failed\n");
    return 1;
  }

  testTokenizer();
  testNestedEmphasis();
  testWordJoining();
  testNonBreakingSpace();
  testPagination(outPrefix);

  printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
