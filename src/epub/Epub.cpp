#include "epub/Epub.h"

#include "epub/FileFeed.h"
#include "epub/XmlPull.h"

namespace {

// Packed "id\0href\0" pairs for the OPF manifest, and null-separated ZIP
// paths in spine order so a TOC target can be resolved to a chapter number.
//
// Both are allocated only for the duration of an import and released after,
// so they cost nothing while reading.
constexpr size_t MANIFEST_ARENA = 6144;
constexpr size_t SPINE_ARENA = 4096;

char* gManifest = nullptr;
size_t gManifestLen = 0;
char* gSpine = nullptr;
size_t gSpineLen = 0;
uint16_t gSpineCount = 0;

bool arenasAcquire() {
  if (!gManifest) gManifest = static_cast<char*>(malloc(MANIFEST_ARENA));
  if (!gSpine) gSpine = static_cast<char*>(malloc(SPINE_ARENA));
  gManifestLen = 0;
  gSpineLen = 0;
  gSpineCount = 0;
  return gManifest && gSpine;
}

void arenasRelease() {
  free(gManifest);
  gManifest = nullptr;
  free(gSpine);
  gSpine = nullptr;
  gManifestLen = 0;
  gSpineLen = 0;
  gSpineCount = 0;
}

void spineReset() {
  gSpineLen = 0;
  gSpineCount = 0;
}

void spineAdd(const char* zipPath) {
  if (!gSpine) return;
  size_t n = strlen(zipPath) + 1;
  if (gSpineLen + n > SPINE_ARENA) return;  // TOC resolution degrades, reading does not
  memcpy(gSpine + gSpineLen, zipPath, n);
  gSpineLen += n;
  gSpineCount++;
}

// Returns the spine index whose path matches, or -1. Compares on the full
// path and falls back to the bare file name, because TOC documents are
// inconsistent about how they spell relative links.
int spineIndexOf(const char* zipPath) {
  if (!gSpine) return -1;
  const char* wantBase = strrchr(zipPath, '/');
  wantBase = wantBase ? wantBase + 1 : zipPath;

  size_t i = 0;
  for (uint16_t idx = 0; idx < gSpineCount && i < gSpineLen; idx++) {
    const char* candidate = gSpine + i;
    i += strlen(candidate) + 1;
    if (strcmp(candidate, zipPath) == 0) return idx;
  }

  i = 0;
  for (uint16_t idx = 0; idx < gSpineCount && i < gSpineLen; idx++) {
    const char* candidate = gSpine + i;
    i += strlen(candidate) + 1;
    const char* base = strrchr(candidate, '/');
    base = base ? base + 1 : candidate;
    if (strcmp(base, wantBase) == 0) return idx;
  }
  return -1;
}

constexpr uint32_t META_MAGIC = 0x4B4E444C;  // "KNDL"
constexpr uint16_t META_VERSION = 1;

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void copyTrimmed(char* dst, size_t dstLen, const char* src) {
  while (*src == ' ' || *src == '\n' || *src == '\t' || *src == '\r') src++;
  size_t n = 0;
  while (src[n] && n < dstLen - 1) n++;
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\n' || src[n - 1] == '\t' ||
                   src[n - 1] == '\r')) {
    n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

void resolveZipPath(const char* baseDir, const char* href, char* out, size_t outLen) {
  char decoded[256];
  size_t d = 0;

  for (const char* p = href; *p && d < sizeof(decoded) - 1; p++) {
    if (*p == '#') break;  // fragment identifier
    if (*p == '%' && hexVal(p[1]) >= 0 && hexVal(p[2]) >= 0) {
      decoded[d++] = static_cast<char>(hexVal(p[1]) * 16 + hexVal(p[2]));
      p += 2;
    } else {
      decoded[d++] = *p;
    }
  }
  decoded[d] = '\0';

  // Absolute hrefs inside the container ignore the OPF directory.
  const char* base = decoded[0] == '/' ? "" : baseDir;
  char joined[320];
  snprintf(joined, sizeof(joined), "%s%s", base, decoded[0] == '/' ? decoded + 1 : decoded);

  // Collapse "." and ".." segments.
  char* segs[32];
  uint8_t depth = 0;
  for (char* tok = strtok(joined, "/"); tok; tok = strtok(nullptr, "/")) {
    if (strcmp(tok, ".") == 0) continue;
    if (strcmp(tok, "..") == 0) {
      if (depth > 0) depth--;
      continue;
    }
    if (depth < 32) segs[depth++] = tok;
  }

  size_t o = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < depth; i++) {
    size_t n = strlen(segs[i]);
    if (o + n + 2 > outLen) break;
    if (o) out[o++] = '/';
    memcpy(out + o, segs[i], n);
    o += n;
  }
  out[o] = '\0';
}

void chapterCachePath(const char* cacheDir, uint16_t chapter, char* out, size_t outLen) {
  snprintf(out, outLen, "%s/ch%03u.xht", cacheDir, chapter);
}

bool EpubImporter::readContainer(char* opfPathOut, size_t outLen) {
  ZipEntry entry;
  if (!zip_.find("META-INF/container.xml", entry)) {
    error_ = "no META-INF/container.xml";
    return false;
  }

  uint8_t buf[1024];
  int n = zip_.extractToBuffer(entry, buf, sizeof(buf) - 1);
  if (n <= 0) {
    error_ = "container.xml unreadable";
    return false;
  }

  MemoryFeed feed(buf, static_cast<uint32_t>(n));
  XmlPull xml;
  xml.begin(&feed, 0);

  while (true) {
    XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok != XmlToken::StartTag) continue;
    if (strcmp(xml.name(), "rootfile") != 0) continue;

    const char* path = xml.attr("full-path");
    if (!path || !*path) continue;

    strncpy(opfPathOut, path, outLen - 1);
    opfPathOut[outLen - 1] = '\0';

    // Remember the OPF's directory; every manifest href is relative to it.
    const char* slash = strrchr(opfPathOut, '/');
    if (slash) {
      size_t n2 = static_cast<size_t>(slash - opfPathOut) + 1;
      if (n2 >= sizeof(opfDir_)) n2 = sizeof(opfDir_) - 1;
      memcpy(opfDir_, opfPathOut, n2);
      opfDir_[n2] = '\0';
    } else {
      opfDir_[0] = '\0';
    }
    return true;
  }

  error_ = "container.xml has no rootfile";
  return false;
}

bool EpubImporter::loadManifest(fs::FS& fs, const char* opfCachePath) {
  gManifestLen = 0;

  FileFeed feed;
  if (!feed.open(fs, opfCachePath)) {
    error_ = "cannot reopen cached OPF";
    return false;
  }

  XmlPull xml;
  xml.begin(&feed, 0);

  bool inMetadata = false;
  bool wantTitle = false, wantAuthor = false;
  bool truncated = false;

  while (true) {
    XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;

    if (tok == XmlToken::StartTag) {
      const char* n = xml.name();

      if (strcmp(n, "metadata") == 0) {
        inMetadata = true;
      } else if (inMetadata && strcmp(n, "title") == 0 && meta_.title[0] == '\0') {
        wantTitle = true;
      } else if (inMetadata && strcmp(n, "creator") == 0 && meta_.author[0] == '\0') {
        wantAuthor = true;
      } else if (strcmp(n, "item") == 0) {
        const char* id = xml.attr("id");
        const char* href = xml.attr("href");
        if (!id || !href) continue;

        size_t need = strlen(id) + strlen(href) + 2;
        if (gManifestLen + need >= MANIFEST_ARENA) {
          truncated = true;
          continue;
        }
        strcpy(gManifest + gManifestLen, id);
        gManifestLen += strlen(id) + 1;
        strcpy(gManifest + gManifestLen, href);
        gManifestLen += strlen(href) + 1;
      }
      continue;
    }

    if (tok == XmlToken::EndTag) {
      if (strcmp(xml.name(), "metadata") == 0) inMetadata = false;
      continue;
    }

    if (tok == XmlToken::Text) {
      if (wantTitle) {
        copyTrimmed(meta_.title, sizeof(meta_.title), xml.text());
        wantTitle = false;
      } else if (wantAuthor) {
        copyTrimmed(meta_.author, sizeof(meta_.author), xml.text());
        wantAuthor = false;
      }
    }
  }

  if (truncated) log_w("epub: manifest larger than %u bytes, some items dropped",
                       static_cast<unsigned>(MANIFEST_ARENA));
  return true;
}

const char* EpubImporter::hrefForId(const char* id) const {
  if (!gManifest) return nullptr;
  size_t i = 0;
  while (i < gManifestLen) {
    const char* key = gManifest + i;
    i += strlen(key) + 1;
    if (i >= gManifestLen) break;
    const char* value = gManifest + i;
    i += strlen(value) + 1;
    if (strcmp(key, id) == 0) return value;
  }
  return nullptr;
}

bool EpubImporter::extractSpine(fs::FS& fs, const char* opfCachePath,
                                const char* cacheDir, Progress progress, void* user) {
  // First pass counts the spine so progress reporting has a denominator.
  uint16_t total = 0;
  {
    FileFeed feed;
    if (!feed.open(fs, opfCachePath)) return false;
    XmlPull xml;
    xml.begin(&feed, 0);
    while (true) {
      XmlToken tok = xml.next();
      if (tok == XmlToken::Eof) break;
      if (tok == XmlToken::StartTag && strcmp(xml.name(), "itemref") == 0) total++;
    }
  }
  if (total == 0) {
    error_ = "spine is empty";
    return false;
  }

  FileFeed feed;
  if (!feed.open(fs, opfCachePath)) return false;
  XmlPull xml;
  xml.begin(&feed, 0);

  uint16_t chapter = 0;
  char zipPath[288];
  char destPath[160];
  spineReset();

  while (true) {
    XmlToken tok = xml.next();
    if (tok == XmlToken::Eof) break;
    if (tok != XmlToken::StartTag || strcmp(xml.name(), "itemref") != 0) continue;

    const char* idref = xml.attr("idref");
    if (!idref) continue;

    const char* href = hrefForId(idref);
    if (!href) {
      log_w("epub: spine item '%s' missing from manifest", idref);
      continue;
    }

    resolveZipPath(opfDir_, href, zipPath, sizeof(zipPath));

    ZipEntry entry;
    if (!zip_.find(zipPath, entry)) {
      log_w("epub: '%s' not present in archive", zipPath);
      continue;
    }

    chapterCachePath(cacheDir, chapter, destPath, sizeof(destPath));
    if (!zip_.extractToFile(entry, fs, destPath)) {
      log_w("epub: failed to extract '%s'", zipPath);
      continue;
    }

    spineAdd(zipPath);
    chapter++;
    if (progress) progress(user, chapter, total, meta_.title);
  }

  meta_.chapterCount = chapter;
  if (chapter == 0) {
    error_ = "no chapters could be extracted";
    return false;
  }
  return true;
}

bool EpubImporter::buildToc(fs::FS& fs, const char* cacheDir, const char* opfCachePath) {
  char tocPath[160];
  snprintf(tocPath, sizeof(tocPath), "%s/%s", cacheDir, kCacheToc);

  File out = fs.open(tocPath, FILE_WRITE);
  if (!out) return false;

  // Prefer the EPUB3 navigation document, fall back to the EPUB2 NCX.
  char navZipPath[288] = {0};
  bool isNcx = false;
  {
    FileFeed feed;
    if (feed.open(fs, opfCachePath)) {
      XmlPull xml;
      xml.begin(&feed, 0);
      char ncxHref[224] = {0};
      bool foundNav = false;

      while (true) {
        XmlToken tok = xml.next();
        if (tok == XmlToken::Eof) break;
        if (tok != XmlToken::StartTag || strcmp(xml.name(), "item") != 0) continue;

        const char* href = xml.attr("href");
        if (!href) continue;

        const char* props = xml.attr("properties");
        if (props && strstr(props, "nav")) {
          resolveZipPath(opfDir_, href, navZipPath, sizeof(navZipPath));
          foundNav = true;
          break;
        }
        const char* type = xml.attr("media-type");
        if (type && strcmp(type, "application/x-dtbncx+xml") == 0) {
          strncpy(ncxHref, href, sizeof(ncxHref) - 1);
          ncxHref[sizeof(ncxHref) - 1] = '\0';
        }
      }
      if (!foundNav && ncxHref[0]) {
        resolveZipPath(opfDir_, ncxHref, navZipPath, sizeof(navZipPath));
        isNcx = true;
      }
    }
  }

  uint16_t written = 0;
  ZipEntry navEntry;

  if (navZipPath[0] && zip_.find(navZipPath, navEntry)) {
    char navCache[160];
    snprintf(navCache, sizeof(navCache), "%s/nav.tmp", cacheDir);

    if (zip_.extractToFile(navEntry, fs, navCache)) {
      // Links inside the nav document are relative to its own directory.
      char navDir[128] = {0};
      const char* slash = strrchr(navZipPath, '/');
      if (slash) {
        size_t n = static_cast<size_t>(slash - navZipPath) + 1;
        if (n >= sizeof(navDir)) n = sizeof(navDir) - 1;
        memcpy(navDir, navZipPath, n);
        navDir[n] = '\0';
      }

      FileFeed feed;
      if (feed.open(fs, navCache)) {
        XmlPull xml;
        xml.begin(&feed, 0);

        char label[64] = {0};
        char target[288] = {0};
        bool inLabelText = false;
        bool inNavLabel = false;
        bool inAnchor = false;

        auto emit = [&]() {
          if (!label[0] || !target[0]) return;
          int chapter = spineIndexOf(target);
          if (chapter < 0) return;

          uint16_t ch = static_cast<uint16_t>(chapter);
          uint16_t len = static_cast<uint16_t>(strlen(label));
          if (len > 63) len = 63;
          out.write(reinterpret_cast<const uint8_t*>(&ch), 2);
          out.write(reinterpret_cast<const uint8_t*>(&len), 2);
          out.write(reinterpret_cast<const uint8_t*>(label), len);
          written++;
          label[0] = '\0';
          target[0] = '\0';
        };

        while (written < 200) {
          XmlToken tok = xml.next();
          if (tok == XmlToken::Eof) break;
          const char* n = xml.name();

          if (tok == XmlToken::StartTag) {
            if (isNcx) {
              // NCX orders the label before the target:
              // <navPoint><navLabel><text>..</text></navLabel><content src=".."/>
              //
              // The <text> must be scoped to <navLabel>, or <docTitle><text>
              // at the top of the file becomes the first entry's label and
              // every label after it is shifted by one.
              if (strcmp(n, "navpoint") == 0) {
                label[0] = '\0';
                target[0] = '\0';
              } else if (strcmp(n, "navlabel") == 0) {
                inNavLabel = true;
              } else if (strcmp(n, "text") == 0 && inNavLabel) {
                inLabelText = true;
              } else if (strcmp(n, "content") == 0) {
                const char* src = xml.attr("src");
                if (src) {
                  resolveZipPath(navDir, src, target, sizeof(target));
                  emit();
                }
              }
            } else {
              // EPUB3 nav puts both on the anchor: <a href="..">Label</a>
              if (strcmp(n, "a") == 0) {
                const char* href = xml.attr("href");
                if (href) {
                  resolveZipPath(navDir, href, target, sizeof(target));
                  inAnchor = true;
                  label[0] = '\0';
                }
              }
            }
            continue;
          }

          if (tok == XmlToken::Text) {
            if ((isNcx && inLabelText) || (!isNcx && inAnchor)) {
              if (label[0] == '\0') copyTrimmed(label, sizeof(label), xml.text());
            }
            continue;
          }

          // EndTag
          if (isNcx && strcmp(n, "text") == 0) {
            inLabelText = false;
          } else if (isNcx && strcmp(n, "navlabel") == 0) {
            inNavLabel = false;
          } else if (!isNcx && strcmp(n, "a") == 0) {
            inAnchor = false;
            emit();
          }
        }
      }
      fs.remove(navCache);
    }
  }

  // A book with no usable navigation still gets one entry per chapter so the
  // TOC screen is never empty.
  if (written == 0) {
    char label[64];
    for (uint16_t c = 0; c < meta_.chapterCount && c < 200; c++) {
      snprintf(label, sizeof(label), "Section %u", static_cast<unsigned>(c + 1));
      uint16_t len = static_cast<uint16_t>(strlen(label));
      out.write(reinterpret_cast<const uint8_t*>(&c), 2);
      out.write(reinterpret_cast<const uint8_t*>(&len), 2);
      out.write(reinterpret_cast<const uint8_t*>(label), len);
      written++;
    }
  }

  out.close();
  log_i("epub: %u TOC entries", written);
  return true;
}

bool EpubImporter::writeMeta(fs::FS& fs, const char* cacheDir) {
  char path[160];
  snprintf(path, sizeof(path), "%s/%s", cacheDir, kCacheMeta);

  File out = fs.open(path, FILE_WRITE);
  if (!out) return false;

  uint32_t magic = META_MAGIC;
  uint16_t version = META_VERSION;
  out.write(reinterpret_cast<const uint8_t*>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const uint8_t*>(&version), sizeof(version));
  out.write(reinterpret_cast<const uint8_t*>(&meta_), sizeof(meta_));
  out.close();
  return true;
}

bool EpubImporter::import(fs::FS& fs, const char* epubPath, const char* cacheDir,
                          Progress progress, void* user) {
  meta_ = BookMeta{};
  error_ = "";

  if (!arenasAcquire()) {
    arenasRelease();
    error_ = "not enough memory to import";
    return false;
  }

  bool ok = importInner(fs, epubPath, cacheDir, progress, user);

  arenasRelease();
  zip_.close();
  return ok;
}

bool EpubImporter::importInner(fs::FS& fs, const char* epubPath,
                               const char* cacheDir, Progress progress,
                               void* user) {
  if (!zip_.open(fs, epubPath)) {
    error_ = "not a readable ZIP archive";
    return false;
  }

  File probe = fs.open(epubPath, FILE_READ);
  if (probe) {
    meta_.sourceSize = probe.size();
    probe.close();
  }

  char opfZipPath[224];
  if (!readContainer(opfZipPath, sizeof(opfZipPath))) return false;

  ZipEntry opfEntry;
  if (!zip_.find(opfZipPath, opfEntry)) {
    error_ = "OPF listed in container.xml is missing";
    return false;
  }

  char opfCachePath[160];
  snprintf(opfCachePath, sizeof(opfCachePath), "%s/%s", cacheDir, kCacheOpf);
  if (!zip_.extractToFile(opfEntry, fs, opfCachePath)) {
    error_ = "could not extract the OPF";
    return false;
  }

  if (!loadManifest(fs, opfCachePath)) return false;
  if (meta_.title[0] == '\0') {
    // Fall back to the file name so the shelf never shows a blank entry.
    const char* base = strrchr(epubPath, '/');
    copyTrimmed(meta_.title, sizeof(meta_.title), base ? base + 1 : epubPath);
  }

  if (!extractSpine(fs, opfCachePath, cacheDir, progress, user)) return false;

  buildToc(fs, cacheDir, opfCachePath);
  writeMeta(fs, cacheDir);
  return true;
}

bool loadBookMeta(fs::FS& fs, const char* cacheDir, BookMeta& out) {
  char path[160];
  snprintf(path, sizeof(path), "%s/%s", cacheDir, kCacheMeta);

  File in = fs.open(path, FILE_READ);
  if (!in) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  bool ok = in.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) == sizeof(magic) &&
            in.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) ==
                sizeof(version) &&
            magic == META_MAGIC && version == META_VERSION &&
            in.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) ==
                static_cast<int>(sizeof(out));
  in.close();
  return ok;
}

uint16_t loadToc(fs::FS& fs, const char* cacheDir, TocItem* items, uint16_t maxItems) {
  char path[160];
  snprintf(path, sizeof(path), "%s/%s", cacheDir, kCacheToc);

  File in = fs.open(path, FILE_READ);
  if (!in) return 0;

  uint16_t count = 0;
  while (count < maxItems) {
    uint16_t chapter = 0, len = 0;
    if (in.read(reinterpret_cast<uint8_t*>(&chapter), 2) != 2) break;
    if (in.read(reinterpret_cast<uint8_t*>(&len), 2) != 2) break;
    if (len >= sizeof(items[count].title)) len = sizeof(items[count].title) - 1;

    if (in.read(reinterpret_cast<uint8_t*>(items[count].title), len) != len) break;
    items[count].title[len] = '\0';
    items[count].chapter = chapter;
    count++;
  }
  in.close();
  return count;
}
