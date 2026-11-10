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

// One NVS key per book, from its id: "p1a2b3c4d" (NVS keys are at most 15
// characters).
static void progressKey(uint32_t bookId, char *out, size_t n) {
  snprintf(out, n, "p%08x", (unsigned)bookId);
}

void storeSaveProgress(uint32_t bookId, uint32_t offset) {
  char key[12];
  progressKey(bookId, key, sizeof(key));
  // One page turn costs one NVS entry: that is what keeps the flash erase
  // budget comfortable at reading speed.
  prefs.putUInt(key, offset);
}

bool storeLoadProgress(uint32_t bookId, uint32_t &offset) {
  char key[12];
  progressKey(bookId, key, sizeof(key));
  if (!prefs.isKey(key)) return false;
  offset = prefs.getUInt(key, 0);
  return true;
}

void storeSaveView(uint8_t view) {
  prefs.putUChar("view", view);
}

uint8_t storeLoadView() {
  return prefs.getUChar("view", 0);
}

// Three more keys, well inside the 15-character NVS limit. Each is one entry,
// written once per Done rather than once per tap.
void storeSaveFontSize(uint8_t index) {
  prefs.putUChar("font", index);
}

uint8_t storeLoadFontSize(uint8_t fallback) {
  return prefs.getUChar("font", fallback);
}

void storeSaveBrightness(uint8_t percent) {
  prefs.putUChar("bright", percent);
}

uint8_t storeLoadBrightness(uint8_t fallback) {
  return prefs.getUChar("bright", fallback);
}

void storeSaveDarkMode(bool on) {
  prefs.putUChar("dark", on ? 1 : 0);
}

bool storeLoadDarkMode(bool fallback) {
  return prefs.getUChar("dark", fallback ? 1 : 0) != 0;
}
