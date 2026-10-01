#include "zip.h"

#include <stdlib.h>
#include <string.h>

#include "hash.h"
#include "miniz.h"

static const uint32_t SIG_END = 0x06054b50;      // end of central directory
static const uint32_t SIG_CENTRAL = 0x02014b50;  // central directory entry
static const uint32_t SIG_LOCAL = 0x04034b50;    // local file header
static const uint32_t END_SIZE = 22;             // end record without comment
static const uint32_t MAX_COMMENT = 0xFFFF;
static const uint32_t MAX_ENTRIES = 4096;
static const uint32_t IN_SIZE = 2048;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool readAt(StorageFile *f, uint32_t pos, void *buf, uint32_t n) {
  return storageSeek(f, pos) && storageRead(f, buf, n) == (int32_t)n;
}

// Finds the end-of-central-directory record. It is the last 22 bytes unless
// the archive has a comment, so try that first, then search backwards
// through the last 64 KB in small chunks.
static bool findEndRecord(StorageFile *f, uint32_t size, uint8_t rec[END_SIZE]) {
  if (size < END_SIZE) return false;
  if (readAt(f, size - END_SIZE, rec, END_SIZE) && le32(rec) == SIG_END) return true;

  uint8_t chunk[256 + 3];
  uint32_t lowest = size > END_SIZE + MAX_COMMENT ? size - END_SIZE - MAX_COMMENT : 0;
  uint32_t end = size - END_SIZE;  // search positions below this
  while (end > lowest) {
    uint32_t start = end > lowest + 256 ? end - 256 : lowest;
    uint32_t n = end - start + 3;
    if (start + n > size) n = size - start;
    if (!readAt(f, start, chunk, n)) return false;
    for (int32_t i = (int32_t)(end - start) - 1; i >= 0; i--) {
      if (i + 4 <= (int32_t)n && le32(chunk + i) == SIG_END) {
        return readAt(f, start + i, rec, END_SIZE);
      }
    }
    end = start;
  }
  return false;
}

bool Zip::open(StorageFile *f) {
  file_ = f;
  index_.clear();
  uint32_t size = storageSize(f);
  uint8_t rec[END_SIZE];
  if (!findEndRecord(f, size, rec)) return false;

  uint16_t entries = le16(rec + 10);
  uint32_t dirSize = le32(rec + 12), dirOffset = le32(rec + 16);
  if (entries == 0xFFFF || dirOffset == 0xFFFFFFFF) return false;  // ZIP64
  if (entries > MAX_ENTRIES || (uint64_t)dirOffset + dirSize > size) return false;
  index_.reserve(entries);

  // Walk the central directory. Names are hashed, never stored.
  uint32_t pos = dirOffset;
  uint8_t hdr[46];
  char name[256];
  for (uint16_t i = 0; i < entries; i++) {
    if (!readAt(f, pos, hdr, sizeof(hdr)) || le32(hdr) != SIG_CENTRAL) return false;
    uint16_t flags = le16(hdr + 8);
    uint16_t nameLen = le16(hdr + 28), extraLen = le16(hdr + 30), commentLen = le16(hdr + 32);
    uint16_t keep = nameLen < sizeof(name) ? nameLen : sizeof(name) - 1;
    if (storageRead(f, name, keep) != keep) return false;
    name[keep] = '\0';

    ZipEntry e = { le32(hdr + 42), le32(hdr + 20), le32(hdr + 24), le16(hdr + 10) };
    bool encrypted = flags & 1;
    bool zip64 = e.compSize == 0xFFFFFFFF || e.size == 0xFFFFFFFF || e.localOffset == 0xFFFFFFFF;
    if (!encrypted && !zip64 && (e.method == 0 || e.method == 8)) {
      index_.push_back({ fnv1a(name), e });
    }
    pos += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

bool Zip::findHash(uint32_t pathHash, ZipEntry &out) const {
  for (const Item &it : index_) {
    if (it.nameHash == pathHash) {
      out = it.entry;
      return true;
    }
  }
  return false;
}

bool Zip::find(const char *path, ZipEntry &out) const {
  return findHash(fnv1a(path), out);
}

// ---- Reading an entry ----

bool ZipEntryReader::begin(StorageFile *f, const ZipEntry &e) {
  end();
  file_ = f;
  entry_ = e;
  failed_ = false;
  done_ = false;

  // The data starts after the local header, whose name and extra field
  // lengths can differ from the central directory's, so read them here.
  uint8_t hdr[30];
  if (!readAt(f, e.localOffset, hdr, sizeof(hdr)) || le32(hdr) != SIG_LOCAL) return false;
  uint32_t dataStart = e.localOffset + 30 + le16(hdr + 26) + le16(hdr + 28);
  if (!storageSeek(f, dataStart)) return false;
  compLeft_ = e.compSize;

  if (e.method == 0) {
    storedLeft_ = e.size;
    return true;
  }

  inflator_ = malloc(sizeof(tinfl_decompressor));
  window_ = (uint8_t *)malloc(TINFL_LZ_DICT_SIZE);
  in_ = (uint8_t *)malloc(IN_SIZE);
  if (!inflator_ || !window_ || !in_) {
    end();
    return false;
  }
  tinfl_init((tinfl_decompressor *)inflator_);
  inPos_ = inLen_ = winPos_ = readyPos_ = readyLen_ = 0;
  return true;
}

int32_t ZipEntryReader::read(uint8_t *buf, uint32_t n) {
  if (failed_ || !file_) return -1;

  if (entry_.method == 0) {
    if (n > storedLeft_) n = storedLeft_;
    int32_t got = n ? storageRead(file_, buf, n) : 0;
    if (got < 0) return failed_ = true, -1;
    storedLeft_ -= (uint32_t)got;
    return got;
  }

  uint32_t given = 0;
  while (given < n) {
    // Hand out bytes the decompressor has already produced.
    if (readyLen_ > 0) {
      uint32_t take = n - given < readyLen_ ? n - given : readyLen_;
      memcpy(buf + given, window_ + readyPos_, take);
      readyPos_ += take;
      readyLen_ -= take;
      given += take;
      continue;
    }
    if (done_) break;

    // Refill the compressed input.
    if (inPos_ == inLen_ && compLeft_ > 0) {
      uint32_t want = compLeft_ < IN_SIZE ? compLeft_ : IN_SIZE;
      int32_t got = storageRead(file_, in_, want);
      if (got <= 0) return failed_ = true, -1;
      inPos_ = 0;
      inLen_ = (uint32_t)got;
      compLeft_ -= (uint32_t)got;
    }

    // Decompress into the window, up to its end; it wraps next time round.
    size_t inBytes = inLen_ - inPos_;
    size_t outBytes = TINFL_LZ_DICT_SIZE - winPos_;
    mz_uint32 flags = compLeft_ > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0;
    tinfl_status status = tinfl_decompress((tinfl_decompressor *)inflator_, in_ + inPos_,
                                           &inBytes, window_, window_ + winPos_, &outBytes, flags);
    inPos_ += (uint32_t)inBytes;
    readyPos_ = winPos_;
    readyLen_ = (uint32_t)outBytes;
    winPos_ = (winPos_ + (uint32_t)outBytes) & (TINFL_LZ_DICT_SIZE - 1);

    if (status == TINFL_STATUS_DONE) {
      done_ = true;
    } else if (status < 0) {
      return failed_ = true, -1;
    } else if (status == TINFL_STATUS_NEEDS_MORE_INPUT && compLeft_ == 0 && inPos_ == inLen_) {
      return failed_ = true, -1;  // the data ended early
    }
  }
  return (int32_t)given;
}

void ZipEntryReader::end() {
  free(inflator_);
  free(window_);
  free(in_);
  inflator_ = nullptr;
  window_ = nullptr;
  in_ = nullptr;
  file_ = nullptr;
}
