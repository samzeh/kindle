#include "hal/Storage.h"

#include <SPI.h>

namespace {
// Conservative for a dev-board wiring harness; still ~1MB/s, far more than a
// page turn needs.
constexpr uint32_t SD_FREQ_HZ = 10000000;
constexpr uint8_t SD_MAX_OPEN_FILES = 8;
}  // namespace

Storage gStorage;

bool Storage::begin() {
  // Display::begin() already brought up SPI with MISO; if the display has not
  // been initialised yet this still works because SPI.begin is idempotent.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  ready_ = SD.begin(PIN_SD_CS, SPI, SD_FREQ_HZ, "/sd", SD_MAX_OPEN_FILES);
  if (!ready_) {
    log_e("SD mount failed on CS=%d -- check the FTS02 chip select pin", PIN_SD_CS);
    return false;
  }

  log_i("SD mounted, %llu MB total", totalBytes() / (1024ULL * 1024ULL));

  ensureDir(DIR_BOOKS);
  ensureDir(DIR_CACHE);
  return true;
}

uint64_t Storage::totalBytes() const {
  return ready_ ? SD.totalBytes() : 0;
}

uint64_t Storage::usedBytes() const {
  return ready_ ? SD.usedBytes() : 0;
}

bool Storage::ensureDir(const char* path) {
  if (!ready_) return false;
  if (SD.exists(path)) return true;

  // Walk the path creating each missing level.
  char buf[128];
  strncpy(buf, path, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  for (char* p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (!SD.exists(buf)) SD.mkdir(buf);
    *p = '/';
  }
  return SD.mkdir(buf) || SD.exists(buf);
}

bool Storage::removeTree(const char* path) {
  if (!ready_) return false;

  File dir = SD.open(path);
  if (!dir) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return SD.remove(path);
  }

  char child[160];
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    snprintf(child, sizeof(child), "%s", entry.path());
    bool isDir = entry.isDirectory();
    entry.close();

    if (isDir) {
      removeTree(child);
    } else {
      SD.remove(child);
    }
  }
  dir.close();
  return SD.rmdir(path);
}

uint32_t Storage::bookId(const char* filename) {
  // FNV-1a. Only needs to be stable and collision-resistant enough to name a
  // cache directory.
  uint32_t hash = 2166136261u;
  for (const char* p = filename; *p; p++) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return hash;
}

void Storage::cacheDir(uint32_t id, char* out, size_t outLen) {
  snprintf(out, outLen, "%s/%08lx", DIR_CACHE, static_cast<unsigned long>(id));
}
