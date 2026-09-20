#include "app/Settings.h"

#include <Preferences.h>

namespace {
constexpr const char* NS_SETTINGS = "kindle";
constexpr const char* NS_PROGRESS = "kprog";
constexpr const char* KEY_BLOB = "cfg";

Preferences gPrefs;

void progressKey(uint32_t bookId, char* out, size_t outLen) {
  // NVS keys are limited to 15 characters.
  snprintf(out, outLen, "p%08lx", static_cast<unsigned long>(bookId));
}
}  // namespace

SettingsStore gSettings;

PageMetrics metricsFor(const Settings& s) {
  PageMetrics m;
  m.x = static_cast<int16_t>(s.margin());
  m.y = 14;
  m.w = static_cast<int16_t>(SCREEN_W - 2 * s.margin());
  m.h = static_cast<int16_t>(SCREEN_H - m.y - STATUS_BAR_H);
  m.fontPx = s.fontPx();
  m.lineSpacingPct = s.lineSpacingPct;
  return m;
}

void SettingsStore::begin() {
  if (!gPrefs.begin(NS_SETTINGS, /*readOnly=*/true)) {
    log_i("settings: no stored config, using defaults");
    return;
  }

  Settings stored;
  size_t got = gPrefs.getBytes(KEY_BLOB, &stored, sizeof(stored));
  gPrefs.end();

  if (got == sizeof(stored)) {
    settings_ = stored;
    // Guard against a blob written by an older build with different tables.
    if (settings_.fontSizeIndex >= FONT_SIZE_COUNT) settings_.fontSizeIndex = FONT_SIZE_DEFAULT;
    if (settings_.marginIndex >= MARGIN_COUNT) settings_.marginIndex = MARGIN_DEFAULT;
    if (settings_.lineSpacingPct < 100 || settings_.lineSpacingPct > 200) {
      settings_.lineSpacingPct = LINE_SPACING_DEFAULT;
    }
    settings_.lastBook[sizeof(settings_.lastBook) - 1] = '\0';
  }
}

void SettingsStore::save() {
  if (!gPrefs.begin(NS_SETTINGS, /*readOnly=*/false)) return;
  gPrefs.putBytes(KEY_BLOB, &settings_, sizeof(settings_));
  gPrefs.end();
}

uint32_t SettingsStore::progressFor(uint32_t bookId) const {
  char key[16];
  progressKey(bookId, key, sizeof(key));

  Preferences prefs;
  if (!prefs.begin(NS_PROGRESS, /*readOnly=*/true)) return 0;
  uint32_t page = prefs.getUInt(key, 0);
  prefs.end();
  return page;
}

void SettingsStore::saveProgress(uint32_t bookId, uint32_t page) {
  char key[16];
  progressKey(bookId, key, sizeof(key));

  Preferences prefs;
  if (!prefs.begin(NS_PROGRESS, /*readOnly=*/false)) return;
  prefs.putUInt(key, page);
  prefs.end();
}
