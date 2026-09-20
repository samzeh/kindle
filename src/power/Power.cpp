#include "power/Power.h"

#include <esp_sleep.h>

#include "config.h"
#include "hal/Display.h"
#include "hal/Touch.h"

Power gPower;

void Power::begin() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  wokeFromTouch_ = (cause == ESP_SLEEP_WAKEUP_EXT0);
  lastActivity_ = millis();

  if (wokeFromTouch_) log_i("power: woke on touch");
}

void Power::deepSleep() {
  log_i("power: sleeping after %u ms idle", static_cast<unsigned>(idleMs()));

  // Park the panel first so it is not left mid-refresh, then drop the touch
  // controller to monitor mode where it still pulls INT low on contact.
  gDisplay.hibernate();
  gTouch.sleep();

  // GPIO36 is RTC_GPIO0, so it can drive ext0 wake. The controller holds INT
  // low for the duration of a touch, hence wake on level 0.
  //
  // GPIO36 has no internal pull resistors on the ESP32; this relies on the
  // pull-up present on the FTS02 adapter. Without it the device would wake
  // immediately and repeatedly.
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(PIN_TOUCH_INT), 0);

  Serial.flush();
  esp_deep_sleep_start();
}
