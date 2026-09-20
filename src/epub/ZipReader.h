#pragma once
#include <Arduino.h>
#include <FS.h>

struct ZipEntry {
  uint32_t localHeaderOffset = 0;
  uint32_t compSize = 0;
  uint32_t uncompSize = 0;
  uint16_t method = 0;  // 0 = stored, 8 = deflate
  bool valid = false;
};

// Receives decompressed bytes. Return false to abort extraction.
using ZipSink = bool (*)(void* user, const uint8_t* data, size_t len);
// Return false to stop iterating.
using ZipVisitor = bool (*)(void* user, const char* name, const ZipEntry& entry);

// Minimal streaming ZIP reader for EPUB containers.
//
// Deliberately not miniz's mz_zip_reader: that keeps the whole central
// directory resident and allocates a 64KB read buffer plus a 32KB window per
// extraction, which does not fit next to a 48KB framebuffer on a board with
// no PSRAM. This walks the central directory from disk and uses only
// miniz's low-level tinfl streaming inflate, peaking around 45KB.
class ZipReader {
 public:
  bool open(fs::FS& fs, const char* path);
  void close();
  bool isOpen() const { return static_cast<bool>(file_); }

  uint16_t entryCount() const { return entryCount_; }

  // Exact-name lookup. Names in a ZIP are '/'-separated and case sensitive.
  bool find(const char* name, ZipEntry& out);
  bool forEach(ZipVisitor visit, void* user);

  bool extract(const ZipEntry& entry, ZipSink sink, void* user);
  bool extractToFile(const ZipEntry& entry, fs::FS& fs, const char* destPath);
  // Returns bytes written, or -1 on failure. For small entries like the OPF.
  int extractToBuffer(const ZipEntry& entry, uint8_t* buf, size_t bufLen);

 private:
  bool readAt(uint32_t offset, void* dst, size_t len);
  bool findEndOfCentralDirectory();
  bool dataOffset(const ZipEntry& entry, uint32_t& out);

  File file_;
  uint32_t fileSize_ = 0;
  uint32_t cdirOffset_ = 0;
  uint32_t cdirSize_ = 0;
  uint16_t entryCount_ = 0;
};
