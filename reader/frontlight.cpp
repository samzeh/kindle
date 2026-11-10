#include "frontlight.h"

#include <Arduino.h>

#include "board_config.h"

// 5 kHz is well above anything the eye can see and low enough that the LEDC
// peripheral can give 10 bits of resolution, which is what makes the dim end
// of the range usable.
static const uint32_t PWM_FREQ = 5000;
static const uint8_t PWM_BITS = 10;
static const uint32_t PWM_MAX = (1u << PWM_BITS) - 1;

static bool attached = false;

void frontlightBegin() {
  attached = ledcAttach(PIN_FRONTLIGHT, PWM_FREQ, PWM_BITS);
  if (!attached) {
    // Same idea as storeBegin's failed mount: say so, rather than leaving a
    // light that silently never comes on.
    Serial.printf("frontlight: could not attach PWM to GPIO%d; the light will "
                  "stay off this session.\n", PIN_FRONTLIGHT);
  }
}

void frontlightSet(uint8_t percent) {
  if (!attached) return;
  if (percent > 100) percent = 100;
  // Perceived brightness is roughly the square root of emitted light, so a
  // linear duty cycle crowds every useful setting into the bottom of the
  // range: 10% and 30% would look nearly the same while 70% and 100% would
  // not. Squaring the percentage spreads the steps out evenly to the eye.
  uint32_t duty = (uint32_t)percent * percent * PWM_MAX / (100 * 100);
  ledcWrite(PIN_FRONTLIGHT, duty);
}
