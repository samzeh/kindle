// Reads files out of a ZIP archive (an EPUB is one), a chunk at a time.
//
// Only what EPUBs need: entries that are stored or deflate-compressed, in
// archives under 4 GB (no ZIP64), without encryption.
#pragma once
#include <stdint.h>
#include <vector>

#include "bytes.h"
#include "storage.h"

struct ZipEntry {
  uint32_t localOffset;  // where the entry's local header starts
  uint32_t compSize;     // bytes stored in the archive
  uint32_t size;         // bytes once decompressed
  uint16_t method;       // 0 = stored, 8 = deflate
};

class Zip {
public:
  // Reads the archive's central directory from an open file. The file must
  // stay open while entries are read. False if it is not a usable ZIP.
  bool open(StorageFile *f);

  // Looks up an entry by its full path inside the archive (case-sensitive,
  // e.g. "OEBPS/content.opf").
  bool find(const char *path, ZipEntry &out) const;
  // The same, by fnv1a(path), for callers that only kept the hash.
  bool findHash(uint32_t pathHash, ZipEntry &out) const;

  StorageFile *file() const { return file_; }
  uint32_t count() const { return (uint32_t)index_.size(); }

private:
  struct Item {
    uint32_t nameHash;
    ZipEntry entry;
  };
  StorageFile *file_ = nullptr;
  std::vector<Item> index_;  // ~20 bytes per entry instead of every name
};

// Streams one entry's decompressed bytes. Deflated entries need about 45 KB
// of heap while open (the decompressor and its 32 KB window); end() or the
// destructor frees it.
class ZipEntryReader : public ByteReader {
public:
  bool begin(StorageFile *f, const ZipEntry &e);
  int32_t read(uint8_t *buf, uint32_t n) override;
  void end();
  ~ZipEntryReader() override { end(); }

private:
  StorageFile *file_ = nullptr;
  ZipEntry entry_ = {};
  uint32_t compLeft_ = 0;     // compressed bytes not yet read from the file
  uint32_t storedLeft_ = 0;   // stored entries: bytes left to hand out
  bool failed_ = false;

  // Deflate state (null for stored entries).
  void *inflator_ = nullptr;  // tinfl_decompressor
  uint8_t *window_ = nullptr; // 32 KB ring the decompressor writes into
  uint8_t *in_ = nullptr;     // compressed input buffer
  uint32_t inPos_ = 0, inLen_ = 0;
  uint32_t winPos_ = 0;       // where the decompressor writes next
  uint32_t readyPos_ = 0, readyLen_ = 0;  // decompressed, not yet handed out
  bool done_ = false;
};
