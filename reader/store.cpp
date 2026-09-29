#include "store.h"
#include <Preferences.h>

static Preferences prefs;

void storeBegin() {
  prefs.begin("reader", false);
}

static void progressKey(uint8_t book, char *out, size_t n) {
  snprintf(out, n, "p%u", (unsigned)book);
}

void storeSaveProgress(uint8_t book, uint32_t offset, bool italic) {
  char key[8];
  progressKey(book, key, sizeof(key));
  // NVS skips a write whose value is unchanged, so repeated saves at the same
  // position cost nothing.
  prefs.putUInt(key, storePack(offset, italic));
}

bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic) {
  char key[8];
  progressKey(book, key, sizeof(key));
  if (!prefs.isKey(key)) return false;
  storeUnpack(prefs.getUInt(key, 0), offset, italic);
  return true;
}

void storeSaveView(uint8_t view) {
  prefs.putUChar("view", view);
}

uint8_t storeLoadView() {
  return prefs.getUChar("view", 0);
}
