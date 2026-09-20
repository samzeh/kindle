#include "epub/PageIndex.h"

#include "epub/Epub.h"
#include "epub/FileFeed.h"
#include "epub/XhtmlSource.h"

namespace {
// A single chapter producing more pages than this means the input is
// corrupt or the layout is not advancing; stop rather than fill the card.
constexpr uint32_t MAX_PAGES_PER_CHAPTER = 4000;
}  // namespace

void PageIndex::path(const char* cacheDir, const PageMetrics& m, char* out,
                     size_t outLen) {
  // Everything that changes where pages break belongs in the name, so a
  // settings change picks up a different index instead of a stale one.
  snprintf(out, outLen, "%s/pages_%u_%u_%u.idx", cacheDir,
           static_cast<unsigned>(m.fontPx), static_cast<unsigned>(m.x),
           static_cast<unsigned>(m.lineSpacingPct));
}

bool PageIndex::exists(fs::FS& fs, const char* cacheDir, const PageMetrics& m) {
  char p[192];
  path(cacheDir, m, p, sizeof(p));
  if (!fs.exists(p)) return false;

  File f = fs.open(p, FILE_READ);
  if (!f) return false;
  bool ok = f.size() >= sizeof(PageRef);
  f.close();
  return ok;
}

bool PageIndex::build(fs::FS& fs, const char* cacheDir, uint16_t chapterCount,
                      const PageMetrics& m, Progress progress, void* user) {
  char indexPath[192];
  path(cacheDir, m, indexPath, sizeof(indexPath));

  char tmpPath[192];
  snprintf(tmpPath, sizeof(tmpPath), "%s/pages.tmp", cacheDir);
  fs.remove(tmpPath);

  File out = fs.open(tmpPath, FILE_WRITE);
  if (!out) {
    log_e("index: cannot create %s", tmpPath);
    return false;
  }

  uint32_t pages = 0;
  char chapterPath[192];

  for (uint16_t c = 0; c < chapterCount; c++) {
    chapterCachePath(cacheDir, c, chapterPath, sizeof(chapterPath));

    FileFeed feed;
    if (!feed.open(fs, chapterPath)) continue;

    XhtmlSource src;
    if (!src.begin(&feed)) continue;

    SourceState cursor;
    for (uint32_t guard = 0; guard < MAX_PAGES_PER_CHAPTER; guard++) {
      if (!src.restore(cursor)) break;

      PageResult r = Layout::layoutPage(src, m, nullptr);

      // Skip pages that render nothing, so an empty chapter file does not
      // become a blank page the reader has to swipe through.
      if (r.lines > 0) {
        PageRef ref;
        ref.chapter = c;
        ref.sub = cursor.sub;
        ref.offset = cursor.offset;
        ref.block = cursor.block;
        ref.emphasis = cursor.emphasis;
        ref.listDepth = cursor.listDepth;
        ref.reserved = 0;
        out.write(reinterpret_cast<const uint8_t*>(&ref), sizeof(ref));
        pages++;
      }

      if (r.endOfContent) break;
      if (r.next.offset == cursor.offset && r.next.sub == cursor.sub) {
        log_w("index: chapter %u stopped advancing at %u", c, cursor.offset);
        break;
      }
      cursor = r.next;
    }

    feed.close();
    if (progress) progress(user, static_cast<uint16_t>(c + 1), chapterCount, pages);
  }

  out.close();

  if (pages == 0) {
    fs.remove(tmpPath);
    log_e("index: no pages produced");
    return false;
  }

  fs.remove(indexPath);
  if (!fs.rename(tmpPath, indexPath)) {
    log_e("index: cannot rename %s -> %s", tmpPath, indexPath);
    fs.remove(tmpPath);
    return false;
  }

  log_i("index: %u pages at %upx", static_cast<unsigned>(pages),
        static_cast<unsigned>(m.fontPx));
  return true;
}

bool PageIndex::open(fs::FS& fs, const char* cacheDir, const PageMetrics& m) {
  close();

  char p[192];
  path(cacheDir, m, p, sizeof(p));
  file_ = fs.open(p, FILE_READ);
  if (!file_) return false;

  pageCount_ = file_.size() / sizeof(PageRef);
  if (pageCount_ == 0) {
    close();
    return false;
  }
  return true;
}

void PageIndex::close() {
  if (file_) file_.close();
  pageCount_ = 0;
}

bool PageIndex::get(uint32_t page, PageRef& out) {
  if (!file_ || page >= pageCount_) return false;
  if (!file_.seek(page * sizeof(PageRef))) return false;
  return file_.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) ==
         static_cast<int>(sizeof(out));
}

uint32_t PageIndex::firstPageOfChapter(uint16_t chapter) {
  if (pageCount_ == 0) return 0;

  // Chapters are non-decreasing across the index, so binary search applies.
  uint32_t lo = 0, hi = pageCount_;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    PageRef ref;
    if (!get(mid, ref)) break;
    if (ref.chapter < chapter) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < pageCount_ ? lo : pageCount_ - 1;
}

uint16_t PageIndex::chapterOfPage(uint32_t page) {
  PageRef ref;
  if (!get(page, ref)) return 0;
  return ref.chapter;
}
