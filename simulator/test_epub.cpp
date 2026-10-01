// Host tests for reading EPUBs: ZIP, XML, ASCII conversion, the package and
// table of contents, and XHTML -> reader text. They use the small books in
// fixtures/library (made by tools/make_fixture_epubs.py).
#include <string>
#include <vector>

#include "ascii.h"
#include "check.h"
#include "convert.h"
#include "epub.h"
#include "hash.h"
#include "host_platform.h"
#include "layout.h"
#include "storage.h"
#include "textformat.h"
#include "textsource.h"
#include "xml.h"
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include "zip.h"

namespace {

struct StringSink : TextSink {
  std::string s;
  bool write(const char *p, uint32_t n) override {
    s.append(p, n);
    return true;
  }
};

// Records parser events as text, to compare runs.
struct Recorder : XmlHandler {
  std::string log;
  void startTag(const char *name, const XmlAttrs &a, bool selfClosing) override {
    log += "<";
    log += name;
    for (const char *attr : { "href", "id", "type" }) {
      const char *v = a.get(attr);
      if (v) log += std::string(" ") + attr + "=" + v;
    }
    log += selfClosing ? "/>" : ">";
  }
  void endTag(const char *name) override { log += std::string("</") + name + ">"; }
  void text(uint32_t cp) override {
    if (cp < 0x80) log += (char)cp;
    else log += "{" + std::to_string(cp) + "}";
  }
};

std::string readAll(ZipEntryReader &r, uint32_t chunk) {
  std::string out;
  std::vector<uint8_t> buf(chunk);
  int32_t n;
  while ((n = r.read(buf.data(), chunk)) > 0) out.append((const char *)buf.data(), n);
  return n < 0 ? "<read error>" : out;
}

// Opens a fixture book; the caller closes the file. nullptr (and a
// failure) if it cannot, in which case the caller stops.
StorageFile *openFixture(const char *name, Zip &zip) {
  std::string path = std::string("/books/") + name;
  StorageFile *f = storageOpen(path.c_str(), false);
  if (f && zip.open(f)) return f;
  printf("FAIL cannot open fixture %s (run the tests from simulator/)\n", name);
  failures++;
  storageClose(f);
  return nullptr;
}

std::string convertXhtml(const char *xhtml) {
  StringSink sink;
  XhtmlConverter conv(sink);
  conv.beginFile(nullptr, nullptr, 0);
  XmlParser parser(conv);
  parser.feed((const uint8_t *)xhtml, (uint32_t)strlen(xhtml));
  conv.endFile();
  return sink.s;
}

// Shows markers readably in failure messages.
std::string visible(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == TXT_HEADING) out += "[H]";
    else if (c == TXT_NOINDENT) out += "[N]";
    else if (c == TXT_ITALIC) out += "[I]";
    else if (c == '\n') out += "|";
    else out += c;
  }
  return out;
}

}  // namespace

static void testZipEntries() {
  Zip zip;
  StorageFile *f = openFixture("aardvark.epub", zip);
  if (!f) return;
  ZipEntry e;
  ZipEntryReader r;

  // A stored entry.
  CHECK(zip.find("mimetype", e));
  CHECK_EQ(e.method, 0);
  CHECK(r.begin(f, e));
  CHECK_STR(readAll(r, 64).c_str(), "application/epub+zip");

  // A deflated entry reads the same whatever the read size, including sizes
  // that split the decompressor's output at awkward points.
  CHECK(zip.find("OEBPS/text/one.xhtml", e));
  CHECK_EQ(e.method, 8);
  CHECK(r.begin(f, e));
  std::string whole = readAll(r, 4096);
  CHECK_EQ(whole.size(), e.size);
  CHECK(whole.find("halfway through the first file") != std::string::npos);
  for (uint32_t chunk : { 1u, 7u, 1000u }) {
    CHECK(r.begin(f, e));
    CHECK(readAll(r, chunk) == whole);
  }
  CHECK(!zip.find("OEBPS/missing.xhtml", e));
  r.end();
  storageClose(f);

  // Written with data descriptors: sizes come from the central directory.
  Zip deep;
  f = openFixture("deep.epub", deep);
  if (!f) return;
  CHECK(deep.find("OPS/text/chapter one.xhtml", e));
  CHECK(r.begin(f, e));
  CHECK(readAll(r, 100).find("A book in a deep folder.") != std::string::npos);
  r.end();
  storageClose(f);

  // Not a ZIP at all.
  Zip notZip;
  f = storageOpen("/books/notes.txt", false);
  CHECK(f && !notZip.open(f));
  storageClose(f);
}

static void testXmlChunking() {
  const char *doc =
    "<?xml version='1.0'?><!DOCTYPE html>\n"
    "<root><!-- a <comment> --><a href=\"x&amp;y>z\" id='q'>T&amp;&#8217;&#x2014;&mdash;&bogus;"
    "</a><br/><![CDATA[<raw> ]]]></root>";
  Recorder whole;
  XmlParser p(whole);
  p.feed((const uint8_t *)doc, (uint32_t)strlen(doc));
  CHECK_STR(whole.log.c_str(),
            "\n<root><a href=x&y>z id=q>T&{8217}{8212}{8212}&bogus;</a><br/><raw> ]</root>");

  // Every possible split into two chunks gives the same events.
  size_t n = strlen(doc);
  for (size_t k = 1; k < n; k++) {
    Recorder split;
    XmlParser q(split);
    q.feed((const uint8_t *)doc, (uint32_t)k);
    q.feed((const uint8_t *)doc + k, (uint32_t)(n - k));
    if (split.log != whole.log) {
      printf("FAIL xml split at %zu: %s\n", k, split.log.c_str());
      failures++;
      break;
    }
  }

  // UTF-8 split across chunks.
  Recorder utf;
  XmlParser u(utf);
  const char *text = "<p>caf\xC3\xA9 \xE2\x80\x94</p>";
  for (const char *c = text; *c; c++) u.feed((const uint8_t *)c, 1);
  CHECK_STR(utf.log.c_str(), "<p>caf{233} {8212}</p>");
}

static void testAscii() {
  char buf[4];
  CHECK_EQ(asciiFor('a', buf), 1);
  CHECK(asciiFor(0x2019, buf) == 1 && buf[0] == '\'');
  CHECK(asciiFor(0x201C, buf) == 1 && buf[0] == '"');
  CHECK(asciiFor(0x2014, buf) == 2 && memcmp(buf, "--", 2) == 0);
  CHECK(asciiFor(0xE9, buf) == 1 && buf[0] == 'e');
  CHECK(asciiFor(0xE6, buf) == 2 && memcmp(buf, "ae", 2) == 0);
  CHECK_EQ(asciiFor(0xAD, buf), 0);  // soft hyphen: dropped
  CHECK(asciiFor(0x4E2D, buf) == 1 && buf[0] == '?');

  char out[32];
  asciiFromUtf8("  \xE2\x80\x9C" "Caf\xC3\xA9\xE2\x80\x9D \n  \xE2\x80\x94 ok  ", out, sizeof(out));
  CHECK_STR(out, "\"Cafe\" -- ok");
  asciiFromUtf8("A very long title indeed", out, 7);
  CHECK_STR(out, "A very");
}

static void testEpubResolve() {
  char out[EPUB_PATH_MAX];
  CHECK_EQ(epubResolve("OPS/package/book.opf", "../text/chapter%20one.xhtml#d1", out, sizeof(out)),
           fnv1a("d1"));
  CHECK_STR(out, "OPS/text/chapter one.xhtml");
  CHECK_EQ(epubResolve("content.opf", "tales.html", out, sizeof(out)), 0);
  CHECK_STR(out, "tales.html");
  epubResolve("x/y/z.opf", "./c/./d.html", out, sizeof(out));
  CHECK_STR(out, "x/y/c/d.html");
  epubResolve("x/z.opf", "../../up.html", out, sizeof(out));
  CHECK_STR(out, "up.html");
  epubResolve("x/z.opf", "/abs/file.html", out, sizeof(out));
  CHECK_STR(out, "abs/file.html");
}

static void testEpubPackages() {
  Zip zip;
  EpubInfo info;
  std::vector<SpineItem> spine;
  std::vector<TocEntry> toc;

  StorageFile *f = openFixture("aardvark.epub", zip);

  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine));
  CHECK_STR(info.title, "Aardvark Stories");
  CHECK_STR(info.author, "Ann Author");
  CHECK_STR(info.identifier, "urn:test:aardvark");
  CHECK_STR(info.coverPath, "OEBPS/images/front.jpg");
  CHECK_STR(info.tocPath, "OEBPS/nav.xhtml");
  CHECK(!info.tocIsNcx);
  CHECK_EQ(spine.size(), 3);
  CHECK(epubReadToc(zip, info, spine, toc));
  CHECK_EQ(toc.size(), 4);  // the landmarks <nav> is not the table of contents
  if (toc.size() == 4) {
    CHECK_STR(toc[0].title, "Chapter 1: The Burrow");
    CHECK_EQ(toc[0].spine, 0);
    CHECK_EQ(toc[0].depth, 0);
    CHECK_EQ(toc[0].fragmentHash, fnv1a("ch1"));
    CHECK_STR(toc[1].title, "A New Scene");
    CHECK_EQ(toc[1].depth, 1);
    CHECK_EQ(toc[2].spine, 1);
    CHECK_EQ(toc[2].fragmentHash, 0);
    CHECK_EQ(toc[3].spine, 2);
  }
  storageClose(f);

  // EPUB 2: NCX table of contents, cover named by <meta name="cover">.
  f = openFixture("bramble.epub", zip);
  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine));
  CHECK_STR(info.author, "Bea Brambleton");
  CHECK_STR(info.coverPath, "art.jpeg");
  CHECK_STR(info.tocPath, "toc.ncx");
  CHECK(info.tocIsNcx);
  CHECK(epubReadToc(zip, info, spine, toc));
  CHECK_EQ(toc.size(), 2);
  if (toc.size() == 2) CHECK_STR(toc[1].title, "Second Tale");
  storageClose(f);

  // No cover, no table of contents.
  f = openFixture("coverless.epub", zip);
  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine));
  CHECK_STR(info.coverPath, "");
  CHECK_STR(info.tocPath, "");
  CHECK(epubReadToc(zip, info, spine, toc));
  CHECK_EQ(toc.size(), 0);
  storageClose(f);

  // Package in a subfolder; linear="no" items are left out of the spine.
  f = openFixture("deep.epub", zip);
  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine));
  CHECK_EQ(spine.size(), 1);
  CHECK(epubReadToc(zip, info, spine, toc));
  CHECK_EQ(toc.size(), 1);
  storageClose(f);
}

static void testConverterExact() {
  std::string got = convertXhtml(
    "<html><head><title>Skip me</title><style>p{}</style></head><body>"
    "<h2 id=\"a\">Title<br/>Line</h2>"
    "<p>One <i>two <em>three</em></i> four &amp; &#8220;five&#8221;</p>"
    "<p><i>open</p><p>plain</p>"
    "<p><i>a<br/>b</i></p>"
    "<hr/><p>after <span class=\"pagenum\">[7]</span>x</p>"
    "</body></html>");
  const std::string want =
    "\x02Title Line\n"
    "\x03One \x01two three \x01" "four & \"five\"\n"
    "\x01open\x01\n"
    "plain\n"
    "\x01" "a\x01\n\x01" "b\x01\n"
    "\x03" "after x";
  if (got != want) {
    printf("FAIL converter output\n  got:  %s\n  want: %s\n", visible(got).c_str(),
           visible(want).c_str());
    failures++;
  }
}

static void testConvertBook() {
  Zip zip;
  EpubInfo info;
  std::vector<SpineItem> spine;
  std::vector<TocEntry> toc;
  std::vector<Chapter> chapters;
  StringSink sink;

  StorageFile *f = openFixture("aardvark.epub", zip);

  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine) && epubReadToc(zip, info, spine, toc));
  CHECK(convertBook(zip, spine, toc, sink, chapters, nullptr, nullptr));
  const std::string &s = sink.s;
  storageClose(f);

  // Each chapter's offset lands on the paragraph it names.
  auto startsWith = [&](uint32_t offset, const char *prefix) {
    return offset < s.size() && s.compare(offset, strlen(prefix), prefix) == 0;
  };
  CHECK_EQ(chapters.size(), 4);
  if (chapters.size() == 4) {
    CHECK_STR(chapters[0].title, "Chapter 1: The Burrow");
    CHECK(startsWith(chapters[0].offset, "\x02" "Chapter 1. The Burrow\n"));
    CHECK_STR(chapters[1].title, "A New Scene");
    CHECK_EQ(chapters[1].depth, 1);
    CHECK(startsWith(chapters[1].offset, "A new scene begins here"));
    CHECK(startsWith(chapters[2].offset, "\x02" "Chapter 2. The Ants"));
    CHECK(startsWith(chapters[3].offset, "\x02" "Chapter 3. Home"));
  }

  CHECK(s.find("WHEN the sun rose") != std::string::npos);  // drop cap
  CHECK(s.find("\"early\" & bright -- very bright") != std::string::npos);
  CHECK(s.find("cafe life") != std::string::npos);
  CHECK(s.find("[12]") == std::string::npos);          // page number
  CHECK(s.find("LICENCE") == std::string::npos);       // Gutenberg header
  CHECK(s.find("\nSo this one must not be italic.") != std::string::npos);

  // Italics never run past the end of a line, and nothing non-ASCII is left.
  int toggles = 0;
  bool clean = true;
  for (unsigned char c : s) {
    if (c == (unsigned char)TXT_ITALIC) toggles++;
    if (c == '\n' && toggles % 2) clean = false;
    if (c >= 0x80) clean = false;
  }
  CHECK(clean);

  // No table of contents: the chapters come from the headings.
  sink.s.clear();
  f = openFixture("coverless.epub", zip);
  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine) && epubReadToc(zip, info, spine, toc));
  CHECK(convertBook(zip, spine, toc, sink, chapters, nullptr, nullptr));
  CHECK_EQ(chapters.size(), 3);
  if (chapters.size() == 3) {
    CHECK_STR(chapters[0].title, "Coverless Notes");
    CHECK_EQ(chapters[0].depth, 0);
    CHECK_STR(chapters[2].title, "Part Two");
    CHECK_EQ(chapters[2].depth, 1);
    CHECK(startsWith(chapters[2].offset, "\x02Part Two"));
  }
  storageClose(f);
}

// Laying out a book read from a file through tiny 64-byte windows gives
// exactly the page breaks of laying it out from memory. This is what lets a
// book far bigger than the ESP32's RAM be read a few KB at a time.
static void testFileTextMatchesMemText() {
  Zip zip;
  EpubInfo info;
  std::vector<SpineItem> spine;
  std::vector<TocEntry> toc;
  std::vector<Chapter> chapters;
  StringSink sink;
  StorageFile *f = openFixture("aardvark.epub", zip);
  if (!f) return;
  CHECK(epubReadPackage(zip, info, spine) && epubReadToc(zip, info, spine, toc));
  CHECK(convertBook(zip, spine, toc, sink, chapters, nullptr, nullptr));
  storageClose(f);

  storageMkdir("/.reader");
  StorageFile *out = storageOpen("/.reader/filetext-test.txt", true);
  CHECK(out && storageWrite(out, sink.s.data(), (uint32_t)sink.s.size()));
  storageClose(out);

  const PageFonts fonts = { &FreeSerif12pt7b, &FreeSerifItalic12pt7b, &FreeSerifBold12pt7b,
                            &FreeSerifBoldItalic12pt7b, &FreeSerif9pt7b };
  GFXcanvas1 canvas(480, 800);
  MemText mem(sink.s.data(), (uint32_t)sink.s.size());
  FileText file(64);
  CHECK(file.open("/.reader/filetext-test.txt"));
  CHECK_EQ(file.length(), sink.s.size());
  PageLayout memLayout(canvas, mem, fonts), fileLayout(canvas, file, fonts);

  PagePos a = { 0, false, false }, b = a;
  int pages = 0;
  bool same = true;
  while (!memLayout.isEnd(a) && pages < 1000) {
    a = memLayout.layoutPage(a, false);
    b = fileLayout.layoutPage(b, false);
    same = same && a.offset == b.offset && a.italic == b.italic && a.inHeading == b.inHeading;
    pages++;
  }
  CHECK(same);
  CHECK(fileLayout.isEnd(b));
  CHECK(pages > 5);  // the fixture spans several pages, or this proves little
  storageRemove("/.reader/filetext-test.txt");
}

static void testPagePack() {
  const PagePos cases[] = { { 0, false, false }, { 123456, true, false },
                            { 0x3FFFFFFF, false, true }, { 7, true, true } };
  for (const PagePos &p : cases) {
    PagePos q = pageUnpack(pagePack(p));
    CHECK(q.offset == p.offset && q.italic == p.italic && q.inHeading == p.inHeading);
  }
}

void runEpubTests() {
  // Storage already points at fixtures/library (tests.cpp's main).
  testZipEntries();
  testXmlChunking();
  testAscii();
  testEpubResolve();
  testEpubPackages();
  testConverterExact();
  testConvertBook();
  testFileTextMatchesMemText();
  testPagePack();
}
