// File storage: where EPUBs are read from and the per-book cache is written.
//
// Paths: "/books/..." holds the .epub files; "/.reader/..." is the reader's
// cache. The EPUB code only uses these functions, so where files actually
// live is up to the implementation:
//   - simulator and tests: simulator/host_platform.cpp (folders on the Mac)
//   - device: storage_sd.cpp (a microSD card; see board_config.h for pins)
#pragma once
#include <stdint.h>

struct StorageFile;  // an open file; only the implementation knows its shape

// Prepares storage. False if there is none (the library then shows no books).
bool storageBegin();

// Opens a file for reading, or (write = true) creates or truncates it for
// writing. nullptr if it cannot.
StorageFile *storageOpen(const char *path, bool write);
// Reads up to n bytes; returns how many were read (0 at the end, -1 on error).
int32_t storageRead(StorageFile *f, void *buf, uint32_t n);
bool storageWrite(StorageFile *f, const void *buf, uint32_t n);
bool storageSeek(StorageFile *f, uint32_t pos);
uint32_t storageSize(StorageFile *f);
void storageClose(StorageFile *f);

bool storageExists(const char *path);
bool storageMkdir(const char *path);
bool storageRename(const char *from, const char *to);  // replaces `to`
bool storageRemove(const char *path);

// Calls fn once for each entry directly inside `dir`.
typedef void (*StorageListFn)(const char *name, uint32_t size, bool isDir, void *ctx);
bool storageList(const char *dir, StorageListFn fn, void *ctx);
