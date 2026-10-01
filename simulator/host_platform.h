// Mac-only controls for the storage stand-in in host_platform.cpp.
#pragma once

// Folders that "/books" and "/.reader" map to (relative to the working
// directory, or absolute).
void hostStorageSetRoots(const char *booksDir, const char *cacheDir);

// Where the store stand-in keeps saved positions between runs (nullptr =
// memory only, the default; the tests use that).
void hostStoreUseFile(const char *path);
// Forgets every saved position and the library view.
void hostStoreReset();
