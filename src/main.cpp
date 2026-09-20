// Custom Kindle: 4.26" frontlit touch e-paper reader on an ESP-WROOM-32.
//
// Boot order matters. The display brings up SPI (including MISO) before the
// SD card mounts on the same bus, and the fonts must be ready before anything
// tries to draw text.

#include <Arduino.h>

#include "app/Settings.h"
#include "config.h"
#include "hal/Display.h"
#include "hal/Storage.h"
#include "hal/Touch.h"
#include "power/Power.h"
#include "text/FontCache.h"
#include "ui/Draw.h"
#include "ui/Ui.h"

namespace {

// Bring-up diagnostics, shown when a peripheral does not come up. Getting
// this on screen is more useful than a serial log when the board is not
// tethered.
void reportFailure(const char* what, const char* hint) {
  log_e("%s: %s", what, hint);
  ui::drawMessage(what, hint);
  gDisplay.present(Display::Refresh::Full);
}

void logMemory(const char* stage) {
  log_i("%s: heap %u free, %u largest block", stage,
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  log_i("kindle booting, reset reason %d", static_cast<int>(esp_reset_reason()));
  logMemory("boot");

  gPower.begin();

  // Display first: it owns SPI.begin() for the bus the SD card also uses.
  gDisplay.begin();
  logMemory("display");

  if (!gFonts.begin()) {
    reportFailure("Font error", "embedded typeface failed to load");
    return;
  }
  logMemory("fonts");

  if (!gTouch.begin()) {
    // Not fatal on its own, but without touch there is no way to navigate.
    reportFailure("No touch panel",
                  "check the 6-pin FPC and I2C on GPIO 32/33");
  }

  if (!gStorage.begin()) {
    // LittleFS formats on failure, so reaching here means the flash itself or
    // the partition table is wrong rather than a first-boot empty filesystem.
    reportFailure("Storage error",
                  "flash partition could not be mounted or formatted");
    return;
  }
  logMemory("sd");

  gSettings.begin();
  gUi.begin();

  logMemory("ready");
}

void loop() {
  gUi.tick();
  delay(10);
}
