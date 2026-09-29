#include "store.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences prefs;

void storeBegin() {
  // Report a failed mount rather than discarding it. With the namespace
  // unopened every getUInt/getUChar returns its default and every put is
  // dropped, so reading positions simply never persist and there is no symptom
  // to chase. Same idea as touchBegin's "no answer at 0x38" line: make the
  // fault announce itself on the serial monitor at startup.
  if (!prefs.begin("reader", false)) {
    Serial.println("store: NVS namespace \"reader\" would not open. Reading "
                   "positions and the library view will not be saved this "
                   "session; erase flash and re-upload if it keeps happening.");
  }
}

static void progressKey(uint8_t book, char *out, size_t n) {
  snprintf(out, n, "p%u", (unsigned)book);
}

void storeSaveProgress(uint8_t book, uint32_t offset, bool italic) {
  char key[8];
  progressKey(book, key, sizeof(key));
  // One page turn costs one NVS entry, not two: offset and italic travel
  // packed into a single uint32 (see storePack, covered by testStorePacking),
  // and each book has one key. That is what keeps the flash erase budget
  // comfortable at reading speed.
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
