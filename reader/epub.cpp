#include "epub.h"

#include <string.h>

#include "ascii.h"
#include "hash.h"
#include "xml.h"

static void copyString(char *out, size_t size, const char *s) {
  strncpy(out, s ? s : "", size - 1);
  out[size - 1] = '\0';
}

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

uint32_t epubResolve(const char *from, const char *href, char *out, size_t outSize) {
  // Start from the folder of `from`, unless href is absolute.
  size_t n = 0;
  if (href[0] != '/') {
    const char *slash = strrchr(from, '/');
    if (slash) {
      n = (size_t)(slash - from) + 1;
      if (n >= outSize) n = outSize - 1;
      memcpy(out, from, n);
    }
  } else {
    href++;
  }

  // Append href up to '#', decoding %XX and applying "." and ".." segments.
  const char *p = href;
  while (*p && *p != '#') {
    const char *segEnd = p;
    while (*segEnd && *segEnd != '/' && *segEnd != '#') segEnd++;
    size_t segLen = (size_t)(segEnd - p);
    if (segLen == 2 && p[0] == '.' && p[1] == '.') {
      if (n > 0) n--;  // drop the trailing '/', then the last segment
      while (n > 0 && out[n - 1] != '/') n--;
    } else if (!(segLen == 1 && p[0] == '.') && segLen > 0) {
      for (const char *c = p; c < segEnd && n + 1 < outSize; c++) {
        int hi, lo;
        if (*c == '%' && c + 2 < segEnd && (hi = hexValue(c[1])) >= 0 &&
            (lo = hexValue(c[2])) >= 0) {
          out[n++] = (char)(hi * 16 + lo);
          c += 2;
        } else {
          out[n++] = *c;
        }
      }
      if (*segEnd == '/' && n + 1 < outSize) out[n++] = '/';
    }
    p = *segEnd == '/' ? segEnd + 1 : segEnd;
  }
  out[n] = '\0';
  return *p == '#' && p[1] ? fnv1a(p + 1) : 0;
}

bool epubParseFile(const Zip &zip, const char *path, XmlHandler &h) {
  ZipEntry e;
  if (!zip.find(path, e)) return false;
  ZipEntryReader r;
  return r.begin(zip.file(), e) && xmlParse(r, h);
}

// ---- container.xml ----

struct ContainerHandler : XmlHandler {
  char opfPath[EPUB_PATH_MAX] = "";
  void startTag(const char *name, const XmlAttrs &a, bool) override {
    if (!opfPath[0] && strcmp(name, "rootfile") == 0) copyString(opfPath, sizeof(opfPath), a.get("full-path"));
  }
};

// ---- The package file, first pass: metadata ----
// The manifest can come before or after <meta name="cover">, so metadata is
// read first and the manifest and spine in a second pass.

struct MetadataHandler : XmlHandler {
  EpubInfo &info;
  AsciiBuilder title, author, identifier;
  AsciiBuilder *capture = nullptr;
  char coverId[64] = "";
  char ncxId[64] = "";

  explicit MetadataHandler(EpubInfo &i)
    : info(i), title(i.title, sizeof(i.title)), author(i.author, sizeof(i.author)),
      identifier(i.identifier, sizeof(i.identifier)) {}

  void startTag(const char *name, const XmlAttrs &a, bool selfClosing) override {
    // Only the first title, creator and identifier count.
    if (strcmp(name, "title") == 0 && title.len == 0) capture = &title;
    else if (strcmp(name, "creator") == 0 && author.len == 0) capture = &author;
    else if (strcmp(name, "identifier") == 0 && identifier.len == 0) capture = &identifier;
    else if (strcmp(name, "meta") == 0) {
      const char *metaName = a.get("name");
      if (metaName && strcmp(metaName, "cover") == 0) copyString(coverId, sizeof(coverId), a.get("content"));
    } else if (strcmp(name, "spine") == 0) {
      copyString(ncxId, sizeof(ncxId), a.get("toc"));
    }
    if (selfClosing) capture = nullptr;
  }
  void endTag(const char *name) override {
    if (strcmp(name, "title") == 0 || strcmp(name, "creator") == 0 || strcmp(name, "identifier") == 0)
      capture = nullptr;
  }
  void text(uint32_t cp) override {
    if (capture) capture->add(cp);
  }
};

// ---- The package file, second pass: manifest and spine ----

struct ManifestHandler : XmlHandler {
  const Zip &zip;
  const char *opfPath;
  EpubInfo &info;
  std::vector<SpineItem> &spine;
  uint32_t coverIdHash, ncxIdHash;
  char fallbackCover[EPUB_PATH_MAX] = "";  // an image with "cover" in its name

  struct Item {
    uint32_t idHash, pathHash;
  };
  std::vector<Item> items;

  ManifestHandler(const Zip &z, const char *opf, EpubInfo &i, std::vector<SpineItem> &s,
                  const char *coverId, const char *ncxId)
    : zip(z), opfPath(opf), info(i), spine(s), coverIdHash(coverId[0] ? fnv1a(coverId) : 0),
      ncxIdHash(ncxId[0] ? fnv1a(ncxId) : 0) {}

  void startTag(const char *name, const XmlAttrs &a, bool) override {
    if (strcmp(name, "item") == 0) {
      const char *id = a.get("id"), *href = a.get("href");
      if (!id || !href) return;
      char path[EPUB_PATH_MAX];
      epubResolve(opfPath, href, path, sizeof(path));
      uint32_t idHash = fnv1a(id);
      items.push_back({ idHash, fnv1a(path) });

      const char *props = a.get("properties");
      const char *type = a.get("media-type");
      bool isImage = type && strncmp(type, "image/", 6) == 0;
      if ((props && strstr(props, "cover-image")) || (coverIdHash && idHash == coverIdHash && isImage)) {
        copyString(info.coverPath, sizeof(info.coverPath), path);
      } else if (isImage && !fallbackCover[0] && strstr(href, "cover")) {
        copyString(fallbackCover, sizeof(fallbackCover), path);
      }
      if (props && strstr(props, "nav")) {  // EPUB 3 navigation document wins
        copyString(info.tocPath, sizeof(info.tocPath), path);
        info.tocIsNcx = false;
      } else if (!info.tocPath[0] &&
                 ((ncxIdHash && idHash == ncxIdHash) ||
                  (type && strcmp(type, "application/x-dtbncx+xml") == 0))) {
        copyString(info.tocPath, sizeof(info.tocPath), path);
        info.tocIsNcx = true;
      }
    } else if (strcmp(name, "itemref") == 0) {
      const char *idref = a.get("idref"), *linear = a.get("linear");
      if (!idref || (linear && strcmp(linear, "no") == 0)) return;
      uint32_t idHash = fnv1a(idref);
      for (const Item &it : items) {
        ZipEntry e;
        if (it.idHash == idHash && zip.findHash(it.pathHash, e)) {
          spine.push_back({ it.pathHash, e });
          break;
        }
      }
    }
  }
};

bool epubReadPackage(const Zip &zip, EpubInfo &info, std::vector<SpineItem> &spine) {
  memset(&info, 0, sizeof(info));
  spine.clear();

  ContainerHandler container;
  if (!epubParseFile(zip, "META-INF/container.xml", container) || !container.opfPath[0]) return false;

  MetadataHandler meta(info);
  if (!epubParseFile(zip, container.opfPath, meta)) return false;

  ManifestHandler manifest(zip, container.opfPath, info, spine, meta.coverId, meta.ncxId);
  if (!epubParseFile(zip, container.opfPath, manifest)) return false;
  if (!info.coverPath[0]) copyString(info.coverPath, sizeof(info.coverPath), manifest.fallbackCover);
  if (!info.title[0]) copyString(info.title, sizeof(info.title), "Untitled");
  return !spine.empty();
}

// ---- Table of contents ----

// Shared by both formats: turns (href, title, depth) into a TocEntry.
struct TocBuilder {
  const EpubInfo &info;
  const std::vector<SpineItem> &spine;
  std::vector<TocEntry> &toc;

  void add(const char *href, const char *title, int depth) {
    if (!href || !title[0]) return;
    char path[EPUB_PATH_MAX];
    uint32_t fragment = epubResolve(info.tocPath, href, path, sizeof(path));
    uint32_t pathHash = fnv1a(path);
    for (uint16_t i = 0; i < spine.size(); i++) {
      if (spine[i].pathHash == pathHash) {
        TocEntry e = { i, fragment, (uint8_t)(depth < 0 ? 0 : depth > 9 ? 9 : depth), "" };
        copyString(e.title, sizeof(e.title), title);
        toc.push_back(e);
        return;
      }
    }
  }
};

// EPUB 3: <nav epub:type="toc"><ol><li><a href="...">Title</a><ol>...
struct NavHandler : XmlHandler {
  TocBuilder &out;
  int navDepth = 0;  // >0 while inside the toc <nav>
  int olDepth = 0;
  char href[EPUB_PATH_MAX] = "";
  char title[48];
  AsciiBuilder label{ title, sizeof(title) };
  bool inLink = false;

  explicit NavHandler(TocBuilder &b) : out(b) {}

  void startTag(const char *name, const XmlAttrs &a, bool selfClosing) override {
    if (strcmp(name, "nav") == 0 && !selfClosing) {
      if (navDepth > 0) navDepth++;
      else {
        const char *type = a.get("type");
        if (type && strstr(type, "toc")) navDepth = 1;
      }
    } else if (navDepth > 0 && strcmp(name, "ol") == 0 && !selfClosing) {
      olDepth++;
    } else if (navDepth > 0 && strcmp(name, "a") == 0 && !selfClosing) {
      copyString(href, sizeof(href), a.get("href"));
      label.clear();
      inLink = true;
    }
  }
  void endTag(const char *name) override {
    if (navDepth == 0) return;
    if (strcmp(name, "nav") == 0) navDepth--;
    else if (strcmp(name, "ol") == 0) olDepth--;
    else if (strcmp(name, "a") == 0 && inLink) {
      inLink = false;
      out.add(href, title, olDepth - 1);
    }
  }
  void text(uint32_t cp) override {
    if (inLink) label.add(cp);
  }
};

// EPUB 2: <navPoint><navLabel><text>Title</text></navLabel><content src="..."/>
//         <navPoint>...nested...</navPoint></navPoint>
struct NcxHandler : XmlHandler {
  TocBuilder &out;
  int depth = 0;
  bool inText = false;
  char title[48];
  AsciiBuilder label{ title, sizeof(title) };

  explicit NcxHandler(TocBuilder &b) : out(b) {}

  void startTag(const char *name, const XmlAttrs &a, bool selfClosing) override {
    if (strcmp(name, "navpoint") == 0 && !selfClosing) {
      depth++;
      label.clear();
    } else if (depth > 0 && strcmp(name, "text") == 0 && !selfClosing) {
      inText = true;
    } else if (depth > 0 && strcmp(name, "content") == 0) {
      out.add(a.get("src"), title, depth - 1);
    }
  }
  void endTag(const char *name) override {
    if (strcmp(name, "navpoint") == 0) depth--;
    else if (strcmp(name, "text") == 0) inText = false;
  }
  void text(uint32_t cp) override {
    if (inText) label.add(cp);
  }
};

bool epubReadToc(const Zip &zip, const EpubInfo &info, const std::vector<SpineItem> &spine,
                 std::vector<TocEntry> &toc) {
  toc.clear();
  if (!info.tocPath[0]) return true;
  TocBuilder builder{ info, spine, toc };
  if (info.tocIsNcx) {
    NcxHandler h(builder);
    return epubParseFile(zip, info.tocPath, h);
  }
  NavHandler h(builder);
  return epubParseFile(zip, info.tocPath, h);
}
