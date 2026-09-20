#pragma once
#include <Arduino.h>

#include "config.h"
#include "text/Layout.h"

struct Settings {
  uint8_t fontSizeIndex = FONT_SIZE_DEFAULT;
  uint8_t marginIndex = MARGIN_DEFAULT;
  uint8_t lineSpacingPct = LINE_SPACING_DEFAULT;
  char lastBook[72] = {0};
  char wifiSsid[33] = {0};
  char wifiPass[65] = {0};

  uint16_t fontPx() const { return FONT_SIZES[fontSizeIndex % FONT_SIZE_COUNT]; }
  uint16_t margin() const { return MARGIN_CHOICES[marginIndex % MARGIN_COUNT]; }
};

// Layout geometry implied by the current settings. Anything that changes the
// result here invalidates a book's page index, which is why the index file
// name is derived from the same three values.
PageMetrics metricsFor(const Settings& s);

// Settings and per-book reading positions in NVS, which survives deep sleep
// and power loss without touching the SD card.
class SettingsStore {
 public:
  void begin();

  Settings& get() { return settings_; }
  const Settings& get() const { return settings_; }
  void save();

  uint32_t progressFor(uint32_t bookId) const;
  void saveProgress(uint32_t bookId, uint32_t page);

 private:
  Settings settings_;
};

extern SettingsStore gSettings;
