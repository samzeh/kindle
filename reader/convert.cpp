#include "convert.h"

#include <algorithm>
#include <string.h>

#include "ascii.h"
#include "hash.h"
#include "textformat.h"

// Words longer than this are split, so a URL or a run of dashes cannot run
// off the edge of the page.
static const uint8_t MAX_WORD = 40;

static bool isOneOf(const char *name, const char *const *list) {
  for (; *list; list++)
    if (strcmp(name, *list) == 0) return true;
  return false;
}

// Tags that start and end a paragraph.
static const char *const BLOCK_TAGS[] = {
  "p", "div", "li", "blockquote", "dt", "dd", "tr", "section", "article", "header",
  "footer", "aside", "figure", "figcaption", "table", "ul", "ol", "dl", "pre", "center",
  "address", "caption", "td", "th", nullptr,
};
static const char *const ITALIC_TAGS[] = { "i", "em", "cite", "dfn", "var", nullptr };
// Tags whose content is never shown.
static const char *const SKIP_TAGS[] = { "head", "style", "script", "svg", "math", nullptr };

static bool classHas(const char *cls, const char *word) {
  if (!cls) return false;
  size_t n = strlen(word);
  for (const char *p = cls; (p = strstr(p, word)); p += n) {
    bool startOk = p == cls || p[-1] == ' ';
    bool endOk = p[n] == '\0' || p[n] == ' ';
    if (startOk && endOk) return true;
  }
  return false;
}

// Elements to leave out entirely: Project Gutenberg's licence header and
// footer, and printed page numbers.
static bool shouldSkip(const char *name, const XmlAttrs &a) {
  if (isOneOf(name, SKIP_TAGS)) return true;
  const char *id = a.get("id");
  if (id && (strcmp(id, "pg-header") == 0 || strcmp(id, "pg-footer") == 0)) return true;
  const char *cls = a.get("class");
  return classHas(cls, "pagenum") || classHas(cls, "x-ebookmaker-pageno");
}

static int headingLevel(const char *name) {
  return name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && name[2] == '\0' ? name[1] - '0' : 0;
}

void XhtmlConverter::put(const char *s, uint32_t n) {
  if (!out_.write(s, n)) failed_ = true;
  length_ += n;
}

void XhtmlConverter::beginFile(const uint32_t *fragments, uint32_t *offsets, uint16_t count,
                               const bool *breakAt) {
  breakParagraph();
  pageBreakNext_ = true;  // each file starts a new page, as on a Kindle
  breakAt_ = breakAt;
  fragments_ = fragments;
  offsets_ = offsets;
  count_ = count;
  pendingCount_ = 0;
  depth_ = 0;
  skipDepth_ = -1;
  italicCount_ = 0;
  headingDepth_ = 0;
  capturingHeading_ = false;
  noIndentNext_ = true;
  // The file's own start (fragment 0) resolves at its first paragraph.
  for (uint16_t i = 0; i < count; i++) {
    offsets[i] = UINT32_MAX;
    if (fragments[i] == 0 && pendingCount_ < MAX_PENDING) pending_[pendingCount_++] = i;
  }
}

void XhtmlConverter::endFile() {
  breakParagraph();
  // Anything never reached points at where the next file's text will start.
  uint32_t next = length_ + (length_ > 0 ? 1 : 0);
  for (uint16_t i = 0; i < count_; i++)
    if (offsets_[i] == UINT32_MAX) offsets_[i] = next;
  fragments_ = nullptr;
  offsets_ = nullptr;
  breakAt_ = nullptr;
  count_ = 0;
}

// Ends the current paragraph. Nothing is written yet: the '\n' goes out when
// the next paragraph starts, so there are no empty paragraphs.
void XhtmlConverter::breakParagraph() {
  italicCount_ = 0;  // see italicAt_
  breakLine();
}

// A line break inside a paragraph (<br/>): the same, except italics carry on
// to the next line.
void XhtmlConverter::breakLine() {
  if (!paraOpen_) return;
  if (italicOn_) {
    put(&TXT_ITALIC, 1);
    italicOn_ = false;
  }
  paraOpen_ = false;
  spacePending_ = false;
  wordLen_ = 0;
}

void XhtmlConverter::startParagraph() {
  bool first = length_ == 0;
  if (!first) put("\n", 1);
  for (uint8_t i = 0; i < pendingCount_; i++) offsets_[pending_[i]] = length_;
  pendingCount_ = 0;
  if (capturingHeading_) headingOffset_ = length_;
  if ((pageBreakNext_ || (tocBreakNext_ && textOnPage_)) && !first) {
    put(&TXT_PAGEBREAK, 1);
    textOnPage_ = false;
  }
  pageBreakNext_ = tocBreakNext_ = false;
  if (headingDepth_ == 0) textOnPage_ = true;
  if (headingDepth_ > 0) {
    put(&TXT_HEADING, 1);
  } else if (noIndentNext_) {
    put(&TXT_NOINDENT, 1);
  }
  noIndentNext_ = false;
  paraOpen_ = true;
  spacePending_ = false;
  wordLen_ = 0;
}

// Writes visible characters, first starting a paragraph, adding a pending
// space and switching italics as needed.
void XhtmlConverter::visible(const char *s, uint8_t n) {
  if (!paraOpen_) startParagraph();
  else if (spacePending_ || wordLen_ >= MAX_WORD) {
    put(" ", 1);
    wordLen_ = 0;
  }
  spacePending_ = false;
  bool wantItalic = italicCount_ > 0;
  if (wantItalic != italicOn_) {
    put(&TXT_ITALIC, 1);
    italicOn_ = wantItalic;
  }
  put(s, n);
  wordLen_ += n;
}

void XhtmlConverter::startTag(const char *name, const XmlAttrs &a, bool selfClosing) {
  if (skipDepth_ >= 0) {
    if (!selfClosing) depth_++;
    return;
  }

  // A table-of-contents target: it resolves when the next paragraph starts.
  const char *id = a.get("id");
  if (id && count_) {
    uint32_t h = fnv1a(id);
    for (uint16_t i = 0; i < count_; i++) {
      if (fragments_[i] == h && offsets_[i] == UINT32_MAX && pendingCount_ < MAX_PENDING) {
        pending_[pendingCount_++] = i;
        if (breakAt_ && breakAt_[i]) tocBreakNext_ = true;
      }
    }
  }

  if (shouldSkip(name, a)) {
    if (!selfClosing) skipDepth_ = depth_++;
    return;
  }
  if (matchesBreakRule(name, a)) pageBreakNext_ = true;
  if (!selfClosing) depth_++;

  int level = headingLevel(name);
  if (level) {
    breakParagraph();
    if (selfClosing) return;
    if (headingDepth_++ == 0) {
      headingLevel_ = (uint8_t)level;
      capturingHeading_ = level <= 3;
      headingBuilder_.clear();
    }
  } else if (strcmp(name, "br") == 0) {
    if (headingDepth_ > 0) {
      spacePending_ = paraOpen_;
      if (capturingHeading_) headingBuilder_.add(' ');
    } else {
      breakLine();
    }
  } else if (strcmp(name, "img") == 0) {
    // Decorative drop caps are images of the first letter, with that letter
    // as their alt text ("W" + "HEN Jane..."). Longer alt text describes a
    // picture, which is left out like the picture itself.
    const char *alt = a.get("alt");
    if (alt && alt[0] && strlen(alt) <= 3) {
      for (const char *c = alt; *c; c++) text((uint8_t)*c);
    }
  } else if (strcmp(name, "hr") == 0) {
    breakParagraph();
    noIndentNext_ = true;
  } else if (isOneOf(name, BLOCK_TAGS)) {
    breakParagraph();
  } else if (!selfClosing && isOneOf(name, ITALIC_TAGS)) {
    if (italicCount_ < MAX_ITALIC) italicAt_[italicCount_++] = depth_;
  }
}

// Whether the element has the class (fnv1a, lower-cased). The class
// attribute is a list; compared lower-cased, as the rules are.
static bool hasClass(const char *cls, uint32_t want) {
  for (const char *p = cls; p && *p;) {
    while (*p == ' ') p++;
    char word[48];
    size_t n = 0;
    for (; *p && *p != ' '; p++)
      if (n + 1 < sizeof(word)) word[n++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
    if (n && fnv1a((const void *)word, n) == want) return true;
  }
  return false;
}

// True if the element asks for a page break before it: in its own style
// attribute, or by the most specific CSS rule from the book's stylesheets
// that matches it (a class outweighs a tag, so ".no-break" can cancel
// "h2"; between equals, the later rule wins).
bool XhtmlConverter::matchesBreakRule(const char *name, const XmlAttrs &a) const {
  const char *style = a.get("style");
  if (style && (strstr(style, "break-before: always") || strstr(style, "break-before:always") ||
                strstr(style, "break-before: page") || strstr(style, "break-before:page")))
    return true;
  if (!ruleCount_) return false;
  uint32_t tag = fnv1a(name);
  const char *cls = a.get("class");
  int best = -1;
  bool breaks = false;
  for (uint16_t r = 0; r < ruleCount_; r++) {
    const BreakRule &rule = rules_[r];
    if (rule.tag && rule.tag != tag) continue;
    if (rule.cls && !hasClass(cls, rule.cls)) continue;
    int weight = (rule.cls ? 10 : 0) + (rule.tag ? 1 : 0);  // as CSS counts it
    if (weight >= best) {
      best = weight;
      breaks = rule.breaks;
    }
  }
  return breaks;
}

void XhtmlConverter::endTag(const char *name) {
  if (skipDepth_ >= 0) {
    if (--depth_ == skipDepth_) skipDepth_ = -1;
    return;
  }
  if (depth_ > 0) depth_--;
  while (italicCount_ > 0 && italicAt_[italicCount_ - 1] > depth_) italicCount_--;

  if (headingLevel(name)) {
    breakParagraph();
    if (headingDepth_ > 0 && --headingDepth_ == 0) {
      noIndentNext_ = true;
      if (capturingHeading_ && headingTitle_[0]) {
        Chapter c = { headingOffset_, (uint8_t)(headingLevel_ - 1), "" };
        strncpy(c.title, headingTitle_, sizeof(c.title) - 1);
        c.title[sizeof(c.title) - 1] = '\0';
        headings.push_back(c);
      }
      capturingHeading_ = false;
    }
  } else if (isOneOf(name, BLOCK_TAGS)) {
    breakParagraph();
  }
}

void XhtmlConverter::text(uint32_t cp) {
  if (skipDepth_ >= 0) return;
  char buf[4];
  uint8_t n = asciiFor(cp, buf);
  if (n == 0) return;

  if (capturingHeading_) headingBuilder_.add(cp);

  if (n == 1 && buf[0] == ' ') {  // whitespace, including newlines in the source
    if (paraOpen_) spacePending_ = true;
    wordLen_ = 0;
    return;
  }
  visible(buf, n);
}

// ---- Whole book ----

bool convertBook(const Zip &zip, const std::vector<SpineItem> &spine,
                 const std::vector<TocEntry> &toc, const std::vector<BreakRule> &rules,
                 TextSink &out, std::vector<Chapter> &chapters,
                 void (*progress)(uint8_t, void *), void *ctx) {
  XhtmlConverter conv(out);
  conv.setBreakRules(rules.data(), (uint16_t)rules.size());
  std::vector<uint32_t> tocOffsets(toc.size(), UINT32_MAX);
  std::vector<uint32_t> fragments;
  std::vector<uint32_t> offsets;
  std::vector<uint16_t> which;
  bool breakAt[256];  // per entry in this file: a top-level chapter

  for (uint16_t s = 0; s < spine.size(); s++) {
    // The table-of-contents entries that point into this file.
    fragments.clear();
    which.clear();
    for (uint16_t t = 0; t < toc.size(); t++) {
      if (toc[t].spine == s && fragments.size() < sizeof(breakAt)) {
        breakAt[fragments.size()] = toc[t].depth == 0;
        fragments.push_back(toc[t].fragmentHash);
        which.push_back(t);
      }
    }
    offsets.assign(fragments.size(), UINT32_MAX);

    ZipEntryReader reader;
    conv.beginFile(fragments.data(), offsets.data(), (uint16_t)fragments.size(), breakAt);
    bool ok = reader.begin(zip.file(), spine[s].entry) && xmlParse(reader, conv);
    conv.endFile();
    if (!ok || conv.failed()) return false;
    for (size_t i = 0; i < which.size(); i++) tocOffsets[which[i]] = offsets[i];
    if (progress) progress((uint8_t)((s + 1) * 100 / spine.size()), ctx);
  }

  chapters.clear();
  for (size_t t = 0; t < toc.size(); t++) {
    Chapter c = { tocOffsets[t], toc[t].depth, "" };
    memcpy(c.title, toc[t].title, sizeof(c.title));
    chapters.push_back(c);
  }
  if (chapters.empty()) chapters = conv.headings;
  std::stable_sort(chapters.begin(), chapters.end(),
                   [](const Chapter &a, const Chapter &b) { return a.offset < b.offset; });
  return true;
}
