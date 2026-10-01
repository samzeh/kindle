// Converts an EPUB's chapter files (XHTML) into the reader's text format
// (textformat.h), streaming: each file is read and written a little at a
// time, so a whole chapter is never in memory.
//
// It also works out where each table-of-contents entry lands in the
// converted text, which gives the chapter list.
#pragma once
#include <stdint.h>
#include <vector>

#include "ascii.h"
#include "epub.h"
#include "xml.h"

// Where converted text goes.
struct TextSink {
  virtual bool write(const char *s, uint32_t n) = 0;
  virtual ~TextSink() {}
};

// A chapter: where it starts in the converted text, and its name.
struct Chapter {
  uint32_t offset;
  uint8_t depth;  // 0 = top level
  char title[48];
};

// Turns one XHTML file at a time into reader text. Exposed (rather than just
// convertBook) so tests can feed it XHTML directly.
class XhtmlConverter : public XmlHandler {
public:
  explicit XhtmlConverter(TextSink &out) : out_(out) {}

  // Starts a new file. `fragments` are the ids (fnv1a; 0 = the file's start)
  // whose position is wanted; each one's text offset is written to the same
  // index of `offsets` when the paragraph after it starts (UINT32_MAX until
  // then). Both arrays must stay valid until endFile.
  void beginFile(const uint32_t *fragments, uint32_t *offsets, uint16_t count);
  void endFile();

  uint32_t length() const { return length_; }
  bool failed() const { return failed_; }

  // Headings (h1-h3) met so far, used as the chapter list when a book has no
  // table of contents.
  std::vector<Chapter> headings;

  void startTag(const char *name, const XmlAttrs &attrs, bool selfClosing) override;
  void endTag(const char *name) override;
  void text(uint32_t codepoint) override;

private:
  void put(const char *s, uint32_t n);
  void breakParagraph();
  void breakLine();
  void startParagraph();
  void visible(const char *s, uint8_t n);

  TextSink &out_;
  uint32_t length_ = 0;
  bool failed_ = false;

  bool paraOpen_ = false;      // the current paragraph has text
  bool spacePending_ = false;  // whitespace seen since the last visible char
  bool noIndentNext_ = true;
  bool italicOn_ = false;      // italics are on in the output
  // Element depth at which each open <i>, <em>, ... started. An italic ends
  // with its element, when anything enclosing it ends, or at any paragraph
  // boundary (inline tags cannot contain paragraphs in valid XHTML), so
  // malformed markup like <p><i>text<p>more cannot italicise the rest of
  // the book.
  static const uint8_t MAX_ITALIC = 8;
  int italicAt_[MAX_ITALIC];
  uint8_t italicCount_ = 0;
  int headingDepth_ = 0;       // inside <h1>..<h6>
  uint8_t headingLevel_ = 0;
  int depth_ = 0;              // element nesting
  int skipDepth_ = -1;         // skipping everything until back at this depth
  uint8_t wordLen_ = 0;

  const uint32_t *fragments_ = nullptr;
  uint32_t *offsets_ = nullptr;
  uint16_t count_ = 0;
  static const uint8_t MAX_PENDING = 16;
  uint16_t pending_[MAX_PENDING];  // fragments waiting for the next paragraph
  uint8_t pendingCount_ = 0;

  char headingTitle_[48];
  AsciiBuilder headingBuilder_{ headingTitle_, sizeof(headingTitle_) };
  bool capturingHeading_ = false;
  uint32_t headingOffset_ = 0;
};

// Converts every spine file, in order, into `out`, and builds the chapter
// list from the table of contents (or, if it has none, from the headings).
// `progress` (may be null) is called with 0-100 after each file.
bool convertBook(const Zip &zip, const std::vector<SpineItem> &spine,
                 const std::vector<TocEntry> &toc, TextSink &out,
                 std::vector<Chapter> &chapters, void (*progress)(uint8_t percent, void *ctx),
                 void *ctx);
