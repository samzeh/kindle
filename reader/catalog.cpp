#include "catalog.h"

#include <Arduino.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cover.h"
#include "epub.h"
#include "hash.h"
#include "storage.h"
#include "zip.h"

// Bump to make every book's cached title, author and cover be re-read.
static const uint16_t META_VERSION = 1;
// Bump when convert.cpp's output changes, so texts are converted again.
static const uint16_t CONVERT_VERSION = 3;  // 3: no-break rules; title pages kept whole
static const uint32_t META_MAGIC = 0x4D455441;  // "META"
static const uint32_t TOC_MAGIC = 0x544F4331;   // "TOC1"
static const uint16_t MAX_BOOKS = 200;

// meta.bin, as written to the cache.
struct MetaFile {
  uint32_t magic;
  uint16_t metaVersion;
  uint16_t convertVersion;  // of text.txt and toc.bin; 0 = not converted yet
  uint32_t textLength;
  char title[64];
  char author[48];
  uint8_t hasCover;
};

static std::vector<BookInfo> books;

uint16_t catalogCount() {
  return (uint16_t)books.size();
}

const BookInfo &catalogBook(uint16_t index) {
  return books[index];
}

void catalogPath(const BookInfo &book, const char *name, char *out, size_t outSize) {
  snprintf(out, outSize, "/.reader/%08x%s%s", (unsigned)book.id, name[0] ? "/" : "", name);
}

static void bookPath(const BookInfo &book, char *out, size_t outSize) {
  snprintf(out, outSize, "/books/%s", book.file);
}

// Writes a whole file under a .tmp name and renames it into place.
static bool writeFile(const char *path, const void *a, uint32_t aLen, const void *b = nullptr,
                      uint32_t bLen = 0) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  StorageFile *f = storageOpen(tmp, true);
  if (!f) return false;
  bool ok = storageWrite(f, a, aLen) && (!b || storageWrite(f, b, bLen));
  storageClose(f);
  if (ok) {
    storageRemove(path);
    ok = storageRename(tmp, path);
  }
  return ok;
}

static bool readMeta(const BookInfo &book, MetaFile &meta) {
  char path[48];
  catalogPath(book, "meta.bin", path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;
  bool ok = storageRead(f, &meta, sizeof(meta)) == (int32_t)sizeof(meta);
  storageClose(f);
  return ok && meta.magic == META_MAGIC && meta.metaVersion == META_VERSION;
}

static bool writeMeta(const BookInfo &book, const MetaFile &meta) {
  char path[48];
  catalogPath(book, "meta.bin", path, sizeof(path));
  return writeFile(path, &meta, sizeof(meta));
}

// Reads a new book's title, author and cover into its cache folder.
static bool importBook(BookInfo &book, MetaFile &meta) {
  char path[128];
  bookPath(book, path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;

  Zip zip;
  EpubInfo *info = new EpubInfo;  // ~700 bytes: keep it off the stack
  std::vector<SpineItem> spine;
  bool ok = zip.open(f) && epubReadPackage(zip, *info, spine);
  if (ok) {
    memset(&meta, 0, sizeof(meta));
    meta.magic = META_MAGIC;
    meta.metaVersion = META_VERSION;
    strncpy(meta.title, info->title, sizeof(meta.title) - 1);
    strncpy(meta.author, info->author, sizeof(meta.author) - 1);

    char dir[48];
    catalogPath(book, "", dir, sizeof(dir));
    storageMkdir("/.reader");
    storageMkdir(dir);

    ZipEntry e;
    if (info->coverPath[0] && zip.find(info->coverPath, e)) {
      uint8_t *bits = (uint8_t *)malloc(COVER_GRID_BYTES + COVER_THUMB_BYTES);
      ZipEntryReader reader;
      if (bits && reader.begin(f, e) && coverRender(reader, bits, bits + COVER_GRID_BYTES)) {
        catalogPath(book, "cover.bin", path, sizeof(path));
        meta.hasCover = writeFile(path, bits, COVER_GRID_BYTES + COVER_THUMB_BYTES);
      }
      free(bits);
    }
    ok = writeMeta(book, meta);  // last: the import is only complete once this exists
  }
  delete info;
  storageClose(f);
  return ok;
}

static bool isEpubName(const char *name) {
  size_t n = strlen(name);
  // Skip hidden files, including macOS's "._" resource forks.
  return name[0] != '.' && n > 5 && strcasecmp(name + n - 5, ".epub") == 0;
}

static void onBooksEntry(const char *name, uint32_t size, bool isDir, void *ctx) {
  std::vector<BookInfo> &list = *(std::vector<BookInfo> *)ctx;
  if (isDir || !isEpubName(name) || strlen(name) >= sizeof(BookInfo::file) || list.size() >= MAX_BOOKS)
    return;
  BookInfo b = {};
  strncpy(b.file, name, sizeof(b.file) - 1);
  b.id = fnv1a(&size, sizeof(size), fnv1a(name));
  list.push_back(b);
}

uint16_t catalogScan(CatalogProgress progress, void *ctx) {
  std::vector<BookInfo> found;
  storageList("/books", onBooksEntry, &found);

  // How many need importing, for the progress count.
  uint16_t toImport = 0;
  MetaFile meta;
  for (const BookInfo &b : found)
    if (!readMeta(b, meta)) toImport++;

  books.clear();
  uint16_t imported = 0;
  for (BookInfo &b : found) {
    if (!readMeta(b, meta)) {
      if (progress) progress(imported, toImport, b.file, ctx);
      bool ok = importBook(b, meta);
      imported++;
      if (!ok) {
        Serial.printf("catalog: %s is not a readable EPUB; skipped\n", b.file);
        continue;
      }
    }
    strncpy(b.title, meta.title, sizeof(b.title) - 1);
    strncpy(b.author, meta.author, sizeof(b.author) - 1);
    b.hasCover = meta.hasCover;
    b.textLength = meta.convertVersion == CONVERT_VERSION ? meta.textLength : 0;
    books.push_back(b);
  }
  std::sort(books.begin(), books.end(),
            [](const BookInfo &a, const BookInfo &b) { return strcasecmp(a.title, b.title) < 0; });
  return imported;
}

// ---- Text and chapters ----

// Buffers converted text on its way to a file.
struct FileSink : TextSink {
  StorageFile *file;
  char buf[1024];
  uint32_t used = 0;
  bool ok = true;
  explicit FileSink(StorageFile *f) : file(f) {}
  bool write(const char *s, uint32_t n) override {
    while (n > 0) {
      uint32_t take = n < sizeof(buf) - used ? n : sizeof(buf) - used;
      memcpy(buf + used, s, take);
      used += take;
      s += take;
      n -= take;
      if (used == sizeof(buf)) flush();
    }
    return ok;
  }
  void flush() {
    if (used && !storageWrite(file, buf, used)) ok = false;
    used = 0;
  }
};

struct ProgressBridge {
  ConvertProgress fn;
  void *ctx;
};

static void onConvertProgress(uint8_t percent, void *ctx) {
  ProgressBridge *p = (ProgressBridge *)ctx;
  if (p->fn) p->fn(percent, p->ctx);
}

bool catalogPrepareText(uint16_t index, ConvertProgress progress, void *ctx) {
  if (index >= books.size()) return false;
  BookInfo &book = books[index];
  MetaFile meta;
  if (!readMeta(book, meta)) return false;
  if (meta.convertVersion == CONVERT_VERSION && book.textLength > 0) return true;

  unsigned long t0 = millis();
  char path[128];
  bookPath(book, path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;

  Zip zip;
  EpubInfo *info = new EpubInfo;
  std::vector<SpineItem> spine;
  std::vector<TocEntry> toc;
  std::vector<BreakRule> rules;
  std::vector<Chapter> chapters;
  bool ok = zip.open(f) && epubReadPackage(zip, *info, spine) && epubReadToc(zip, *info, spine, toc) &&
            epubReadBreakRules(zip, *info, rules);
  delete info;

  char textPath[48], tmpPath[52];
  catalogPath(book, "text.txt", textPath, sizeof(textPath));
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", textPath);
  uint32_t length = 0;
  if (ok) {
    StorageFile *out = storageOpen(tmpPath, true);
    FileSink *sink = out ? new FileSink(out) : nullptr;
    ProgressBridge bridge = { progress, ctx };
    ok = sink && convertBook(zip, spine, toc, rules, *sink, chapters, onConvertProgress, &bridge);
    if (sink) {
      sink->flush();
      ok = ok && sink->ok;
      delete sink;
    }
    if (out) length = storageSize(out);
    storageClose(out);
  }
  storageClose(f);
  if (ok) {
    storageRemove(textPath);
    ok = storageRename(tmpPath, textPath);
  }

  // The chapter list: a count, then the Chapter records.
  if (ok) {
    char tocPath[48];
    catalogPath(book, "toc.bin", tocPath, sizeof(tocPath));
    uint32_t header[2] = { TOC_MAGIC, (uint32_t)chapters.size() };
    ok = writeFile(tocPath, header, sizeof(header), chapters.data(),
                   (uint32_t)(chapters.size() * sizeof(Chapter)));
  }
  if (ok) {
    meta.convertVersion = CONVERT_VERSION;
    meta.textLength = length;
    ok = writeMeta(book, meta);
    book.textLength = length;
    Serial.printf("catalog: converted %s: %u bytes, %u chapters in %lu ms\n", book.title,
                  (unsigned)length, (unsigned)chapters.size(), millis() - t0);
  }
  return ok;
}

bool catalogLoadChapters(const BookInfo &book, std::vector<Chapter> &chapters) {
  chapters.clear();
  char path[48];
  catalogPath(book, "toc.bin", path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;
  uint32_t header[2];
  bool ok = storageRead(f, header, sizeof(header)) == (int32_t)sizeof(header) &&
            header[0] == TOC_MAGIC && header[1] < 10000;
  if (ok) {
    chapters.resize(header[1]);
    uint32_t bytes = header[1] * sizeof(Chapter);
    ok = bytes == 0 || storageRead(f, chapters.data(), bytes) == (int32_t)bytes;
  }
  storageClose(f);
  if (!ok) chapters.clear();
  return ok;
}

// ---- Full-screen cover ----

// The canvas's buffer, in the panel's own layout whatever the rotation.
static uint32_t canvasBytes(GFXcanvas1 &canvas) {
  int16_t rawW = canvas.getRotation() % 2 ? canvas.height() : canvas.width();
  int16_t rawH = canvas.getRotation() % 2 ? canvas.width() : canvas.height();
  return (uint32_t)((rawW + 7) / 8) * rawH;
}

bool catalogLoadFullCover(const BookInfo &book, GFXcanvas1 &canvas) {
  char path[48];
  catalogPath(book, "cover-page.bin", path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;
  uint32_t bytes = canvasBytes(canvas);
  bool ok = storageSize(f) == bytes && storageRead(f, canvas.getBuffer(), bytes) == (int32_t)bytes;
  storageClose(f);
  return ok;
}

bool catalogPrepareFullCover(uint16_t index, GFXcanvas1 &canvas) {
  if (index >= books.size() || !books[index].hasCover) return false;
  const BookInfo &book = books[index];
  if (catalogLoadFullCover(book, canvas)) return true;

  char path[128];
  bookPath(book, path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;
  Zip zip;
  EpubInfo *info = new EpubInfo;
  std::vector<SpineItem> spine;
  ZipEntry e;
  ZipEntryReader reader;
  bool ok = zip.open(f) && epubReadPackage(zip, *info, spine) && info->coverPath[0] &&
            zip.find(info->coverPath, e) && reader.begin(f, e) && coverRenderFull(reader, canvas);
  delete info;
  reader.end();
  storageClose(f);
  if (ok) {
    catalogPath(book, "cover-page.bin", path, sizeof(path));
    ok = writeFile(path, canvas.getBuffer(), canvasBytes(canvas));
  }
  return ok;
}
