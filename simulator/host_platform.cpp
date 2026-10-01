// The Mac's stand-ins for the device's book storage (reader/storage.h) and
// saved settings (reader/store.h), shared by the simulator and the tests.
// "/books/..." maps to one folder of .epub files and "/.reader/..." to a
// cache folder; hostStorageSetRoots picks them.
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "host_platform.h"
#include "storage.h"

static std::string booksRoot = "books";
static std::string cacheRoot = ".cache";

void hostStorageSetRoots(const char *booksDir, const char *cacheDir) {
  booksRoot = booksDir;
  cacheRoot = cacheDir;
}

// Maps a storage path to a path on the Mac, or "" if it is outside both roots.
static std::string hostPath(const char *path) {
  auto under = [&](const char *prefix, const std::string &root) -> std::string {
    size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) return "";
    if (path[n] != '\0' && path[n] != '/') return "";
    return root + (path + n);
  };
  std::string p = under("/books", booksRoot);
  if (p.empty()) p = under("/.reader", cacheRoot);
  return p;
}

struct StorageFile {
  FILE *fp;
};

bool storageBegin() {
  mkdir(cacheRoot.c_str(), 0755);
  struct stat st;
  return stat(booksRoot.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

StorageFile *storageOpen(const char *path, bool write) {
  std::string p = hostPath(path);
  if (p.empty()) return nullptr;
  FILE *fp = fopen(p.c_str(), write ? "wb" : "rb");
  if (!fp) return nullptr;
  return new StorageFile{ fp };
}

int32_t storageRead(StorageFile *f, void *buf, uint32_t n) {
  size_t got = fread(buf, 1, n, f->fp);
  if (got == 0 && ferror(f->fp)) return -1;
  return (int32_t)got;
}

bool storageWrite(StorageFile *f, const void *buf, uint32_t n) {
  return fwrite(buf, 1, n, f->fp) == n;
}

bool storageSeek(StorageFile *f, uint32_t pos) {
  return fseek(f->fp, (long)pos, SEEK_SET) == 0;
}

uint32_t storageSize(StorageFile *f) {
  long here = ftell(f->fp);
  fseek(f->fp, 0, SEEK_END);
  long size = ftell(f->fp);
  fseek(f->fp, here, SEEK_SET);
  return (uint32_t)size;
}

void storageClose(StorageFile *f) {
  if (!f) return;
  fclose(f->fp);
  delete f;
}

bool storageExists(const char *path) {
  std::string p = hostPath(path);
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0;
}

bool storageMkdir(const char *path) {
  std::string p = hostPath(path);
  return !p.empty() && (mkdir(p.c_str(), 0755) == 0 || storageExists(path));
}

bool storageRename(const char *from, const char *to) {
  std::string a = hostPath(from), b = hostPath(to);
  return !a.empty() && !b.empty() && rename(a.c_str(), b.c_str()) == 0;
}

bool storageRemove(const char *path) {
  std::string p = hostPath(path);
  return !p.empty() && remove(p.c_str()) == 0;
}

bool storageList(const char *dir, StorageListFn fn, void *ctx) {
  std::string p = hostPath(dir);
  DIR *d = p.empty() ? nullptr : opendir(p.c_str());
  if (!d) return false;
  while (struct dirent *e = readdir(d)) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    struct stat st;
    std::string full = p + "/" + e->d_name;
    if (stat(full.c_str(), &st) != 0) continue;
    fn(e->d_name, (uint32_t)st.st_size, S_ISDIR(st.st_mode), ctx);
  }
  closedir(d);
  return true;
}

// ---- Stand-in for NVS (reader/store.h) ----
// Kept in memory, and optionally mirrored to a file so the simulator
// remembers reading positions between runs, as the device does.

#include <map>

#include "store.h"

static std::map<uint32_t, uint32_t> progress;
static uint8_t libraryView = 0;
static std::string stateFile;  // "" = memory only

static void stateWrite() {
  if (stateFile.empty()) return;
  FILE *f = fopen(stateFile.c_str(), "w");
  if (!f) return;
  fprintf(f, "view %u\n", libraryView);
  for (const auto &p : progress) fprintf(f, "%08x %u\n", p.first, p.second);
  fclose(f);
}

void hostStoreUseFile(const char *path) {
  stateFile = path ? path : "";
}

void hostStoreReset() {
  progress.clear();
  libraryView = 0;
}

void storeBegin() {
  hostStoreReset();
  if (stateFile.empty()) return;
  FILE *f = fopen(stateFile.c_str(), "r");
  if (!f) return;
  unsigned view, id, offset;
  if (fscanf(f, "view %u\n", &view) == 1) libraryView = (uint8_t)view;
  while (fscanf(f, "%x %u\n", &id, &offset) == 2) progress[id] = offset;
  fclose(f);
}

void storeSaveProgress(uint32_t bookId, uint32_t offset) {
  progress[bookId] = offset;
  stateWrite();
}

bool storeLoadProgress(uint32_t bookId, uint32_t &offset) {
  auto it = progress.find(bookId);
  if (it == progress.end()) return false;
  offset = it->second;
  return true;
}

void storeSaveView(uint8_t view) {
  libraryView = view;
  stateWrite();
}

uint8_t storeLoadView() {
  return libraryView;
}
