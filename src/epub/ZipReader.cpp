#include "epub/ZipReader.h"

#include "miniz.h"

namespace {

constexpr uint32_t SIG_EOCD = 0x06054B50;
constexpr uint32_t SIG_CDIR = 0x02014B50;
constexpr uint32_t SIG_LOCAL = 0x04034B50;

constexpr uint16_t EOCD_SIZE = 22;
constexpr uint16_t CDIR_HEADER_SIZE = 46;
constexpr uint16_t LOCAL_HEADER_SIZE = 30;

// The EOCD may be followed by up to 64KB of archive comment.
constexpr uint32_t MAX_COMMENT = 65535;

constexpr size_t INFLATE_IN_BUF = 2048;
constexpr size_t MAX_NAME = 256;

inline uint16_t rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct FileSinkCtx {
  File* out;
  bool ok;
};

bool fileSink(void* user, const uint8_t* data, size_t len) {
  auto* ctx = static_cast<FileSinkCtx*>(user);
  if (ctx->out->write(data, len) != len) {
    ctx->ok = false;
    return false;
  }
  return true;
}

struct BufferSinkCtx {
  uint8_t* buf;
  size_t cap;
  size_t used;
};

bool bufferSink(void* user, const uint8_t* data, size_t len) {
  auto* ctx = static_cast<BufferSinkCtx*>(user);
  if (ctx->used + len > ctx->cap) return false;
  memcpy(ctx->buf + ctx->used, data, len);
  ctx->used += len;
  return true;
}

}  // namespace

bool ZipReader::open(fs::FS& fs, const char* path) {
  close();
  file_ = fs.open(path, FILE_READ);
  if (!file_) return false;

  fileSize_ = file_.size();
  if (fileSize_ < EOCD_SIZE) {
    close();
    return false;
  }
  if (!findEndOfCentralDirectory()) {
    log_e("zip: no end-of-central-directory in %s", path);
    close();
    return false;
  }
  return true;
}

void ZipReader::close() {
  if (file_) file_.close();
  fileSize_ = 0;
  cdirOffset_ = 0;
  cdirSize_ = 0;
  entryCount_ = 0;
}

bool ZipReader::readAt(uint32_t offset, void* dst, size_t len) {
  if (offset + len > fileSize_) return false;
  if (!file_.seek(offset)) return false;
  return file_.read(static_cast<uint8_t*>(dst), len) == static_cast<int>(len);
}

bool ZipReader::findEndOfCentralDirectory() {
  // Scan backwards in overlapping windows so the signature is never split.
  constexpr uint32_t WINDOW = 1024;
  uint8_t buf[WINDOW + 3];

  uint32_t maxScan = fileSize_ < (MAX_COMMENT + EOCD_SIZE) ? fileSize_
                                                           : MAX_COMMENT + EOCD_SIZE;
  uint32_t scanned = 0;

  while (scanned < maxScan) {
    uint32_t chunk = maxScan - scanned;
    if (chunk > WINDOW) chunk = WINDOW;

    uint32_t start = fileSize_ - scanned - chunk;
    uint32_t overlap = (scanned == 0) ? 0 : 3;
    if (!readAt(start, buf, chunk + overlap)) return false;

    for (int32_t i = static_cast<int32_t>(chunk) - 1; i >= 0; i--) {
      if (rd32(buf + i) != SIG_EOCD) continue;

      uint32_t eocd = start + static_cast<uint32_t>(i);
      uint8_t hdr[EOCD_SIZE];
      if (!readAt(eocd, hdr, EOCD_SIZE)) return false;

      entryCount_ = rd16(hdr + 10);
      cdirSize_ = rd32(hdr + 12);
      cdirOffset_ = rd32(hdr + 16);

      if (cdirOffset_ == 0xFFFFFFFF || entryCount_ == 0xFFFF) {
        log_e("zip: ZIP64 archives are not supported");
        return false;
      }
      return cdirOffset_ + cdirSize_ <= fileSize_;
    }
    scanned += chunk;
  }
  return false;
}

bool ZipReader::forEach(ZipVisitor visit, void* user) {
  if (!file_) return false;

  uint32_t offset = cdirOffset_;
  uint8_t hdr[CDIR_HEADER_SIZE];
  char name[MAX_NAME];

  for (uint16_t i = 0; i < entryCount_; i++) {
    if (!readAt(offset, hdr, CDIR_HEADER_SIZE)) return false;
    if (rd32(hdr) != SIG_CDIR) return false;

    ZipEntry e;
    e.method = rd16(hdr + 10);
    e.compSize = rd32(hdr + 20);
    e.uncompSize = rd32(hdr + 24);
    e.localHeaderOffset = rd32(hdr + 42);
    e.valid = true;

    uint16_t nameLen = rd16(hdr + 28);
    uint16_t extraLen = rd16(hdr + 30);
    uint16_t commentLen = rd16(hdr + 32);

    uint16_t copy = nameLen < MAX_NAME - 1 ? nameLen : MAX_NAME - 1;
    if (!readAt(offset + CDIR_HEADER_SIZE, name, copy)) return false;
    name[copy] = '\0';

    if (!visit(user, name, e)) return false;

    offset += CDIR_HEADER_SIZE + nameLen + extraLen + commentLen;
  }
  return true;
}

namespace {
struct FindCtx {
  const char* want;
  ZipEntry* out;
  bool found;
};

bool findVisitor(void* user, const char* name, const ZipEntry& entry) {
  auto* ctx = static_cast<FindCtx*>(user);
  if (strcmp(name, ctx->want) != 0) return true;
  *ctx->out = entry;
  ctx->found = true;
  return false;  // stop
}
}  // namespace

bool ZipReader::find(const char* name, ZipEntry& out) {
  FindCtx ctx{name, &out, false};
  forEach(findVisitor, &ctx);
  return ctx.found;
}

bool ZipReader::dataOffset(const ZipEntry& entry, uint32_t& out) {
  uint8_t hdr[LOCAL_HEADER_SIZE];
  if (!readAt(entry.localHeaderOffset, hdr, LOCAL_HEADER_SIZE)) return false;
  if (rd32(hdr) != SIG_LOCAL) return false;

  // The local header's name and extra lengths can differ from the central
  // directory's, so they must be read here rather than reused.
  out = entry.localHeaderOffset + LOCAL_HEADER_SIZE + rd16(hdr + 26) + rd16(hdr + 28);
  return out <= fileSize_;
}

bool ZipReader::extract(const ZipEntry& entry, ZipSink sink, void* user) {
  if (!file_ || !entry.valid) return false;

  uint32_t offset;
  if (!dataOffset(entry, offset)) return false;
  if (!file_.seek(offset)) return false;

  if (entry.method == 0) {
    uint8_t buf[INFLATE_IN_BUF];
    uint32_t remaining = entry.compSize;
    while (remaining > 0) {
      size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
      int got = file_.read(buf, want);
      if (got <= 0) return false;
      if (!sink(user, buf, static_cast<size_t>(got))) return false;
      remaining -= static_cast<uint32_t>(got);
    }
    return true;
  }

  if (entry.method != MZ_DEFLATED) {
    log_e("zip: unsupported compression method %u", entry.method);
    return false;
  }

  // ~11KB for the Huffman tables plus a 32KB sliding window, both released
  // as soon as the entry is done.
  auto* decomp = static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
  auto* dict = static_cast<uint8_t*>(malloc(TINFL_LZ_DICT_SIZE));
  if (!decomp || !dict) {
    free(decomp);
    free(dict);
    log_e("zip: out of memory for inflate (need %u bytes)",
          static_cast<unsigned>(sizeof(tinfl_decompressor) + TINFL_LZ_DICT_SIZE));
    return false;
  }
  tinfl_init(decomp);

  uint8_t inBuf[INFLATE_IN_BUF];
  size_t inAvail = 0, inPos = 0;
  size_t dictOfs = 0;
  uint32_t remaining = entry.compSize;
  bool ok = false;

  for (;;) {
    if (inPos == inAvail) {
      size_t want = remaining < sizeof(inBuf) ? remaining : sizeof(inBuf);
      if (want == 0) {
        inAvail = inPos = 0;
      } else {
        int got = file_.read(inBuf, want);
        if (got <= 0) break;
        inAvail = static_cast<size_t>(got);
        inPos = 0;
        remaining -= static_cast<uint32_t>(got);
      }
    }

    size_t inBytes = inAvail - inPos;
    size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
    // No zlib wrapper: ZIP stores raw DEFLATE.
    uint32_t flags = remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0;

    tinfl_status status = tinfl_decompress(decomp, inBuf + inPos, &inBytes, dict,
                                           dict + dictOfs, &outBytes, flags);
    inPos += inBytes;

    if (outBytes > 0 && !sink(user, dict + dictOfs, outBytes)) break;
    dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);

    if (status == TINFL_STATUS_DONE) {
      ok = true;
      break;
    }
    if (status < TINFL_STATUS_DONE) {
      log_e("zip: inflate failed with status %d", static_cast<int>(status));
      break;
    }
    if (status == TINFL_STATUS_NEEDS_MORE_INPUT && remaining == 0 && inBytes == 0) {
      break;  // truncated stream
    }
  }

  free(decomp);
  free(dict);
  return ok;
}

bool ZipReader::extractToFile(const ZipEntry& entry, fs::FS& fs, const char* destPath) {
  File out = fs.open(destPath, FILE_WRITE);
  if (!out) return false;

  FileSinkCtx ctx{&out, true};
  bool ok = extract(entry, fileSink, &ctx) && ctx.ok;
  out.close();

  if (!ok) fs.remove(destPath);
  return ok;
}

int ZipReader::extractToBuffer(const ZipEntry& entry, uint8_t* buf, size_t bufLen) {
  BufferSinkCtx ctx{buf, bufLen, 0};
  if (!extract(entry, bufferSink, &ctx)) return -1;
  return static_cast<int>(ctx.used);
}
