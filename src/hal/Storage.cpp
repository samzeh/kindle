#include "hal/Storage.h"

namespace {
// Matches the label in partitions/kindle_4mb.csv.
constexpr const char* PARTITION_LABEL = "littlefs";
constexpr const char* MOUNT_POINT = "/littlefs";
// Chapter file, page index, and an upload or extraction target at once.
constexpr uint8_t MAX_OPEN_FILES = 8;
}  // namespace

Storage gStorage;

bool Storage::begin() {
  // Format on failure so a freshly flashed board comes up usable rather than
  // reporting a storage error the user cannot act on.
  ready_ = LittleFS.begin(/*formatOnFail=*/true, MOUNT_POINT, MAX_OPEN_FILES,
                          PARTITION_LABEL);
  if (!ready_) {
    log_e("LittleFS mount failed even after formatting");
    return false;
  }

  log_i("LittleFS mounted: %llu KB total, %llu KB free", totalBytes() / 1024,
        freeBytes() / 1024);

  ensureDir(DIR_BOOKS);
  ensureDir(DIR_CACHE);
  return true;
}

uint64_t Storage::totalBytes() const {
  return ready_ ? LittleFS.totalBytes() : 0;
}

uint64_t Storage::usedBytes() const {
  return ready_ ? LittleFS.usedBytes() : 0;
}

uint64_t Storage::freeBytes() const {
  if (!ready_) return 0;
  uint64_t total = LittleFS.totalBytes();
  uint64_t used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

bool Storage::ensureDir(const char* path) {
  if (!ready_) return false;
  if (LittleFS.exists(path)) return true;

  // Walk the path creating each missing level.
  char buf[128];
  strncpy(buf, path, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  for (char* p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (!LittleFS.exists(buf)) LittleFS.mkdir(buf);
    *p = '/';
  }
  return LittleFS.mkdir(buf) || LittleFS.exists(buf);
}

bool Storage::removeTree(const char* path) {
  if (!ready_) return false;

  File dir = LittleFS.open(path);
  if (!dir) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return LittleFS.remove(path);
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
      LittleFS.remove(child);
    }
  }
  dir.close();
  return LittleFS.rmdir(path);
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
