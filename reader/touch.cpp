#include "touch.h"
#include <Wire.h>
#include "board_config.h"

static const uint8_t FT6336_ADDR = 0x38;
static const uint8_t REG_TD_STATUS = 0x02;  // low nibble = number of touch points
static const uint8_t REG_P1_XH = 0x03;      // P1_XH, P1_XL, P1_YH, P1_YL

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(FT6336_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void touchBegin() {
  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);  // FT6336 needs ~300 ms after reset before it answers on I2C
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  // Report whether the touch chip answers, to make wiring problems obvious.
  uint8_t id;
  if (readRegs(0xA3, &id, 1)) {
    // Same settings as the vendor demo, whose touch response was good.
    writeReg(0x00, 0);   // normal operating mode
    writeReg(0x80, 22);  // touch threshold: lower = more sensitive
    writeReg(0x88, 14);  // scan rate while touched (reports per second)
    uint8_t threshold = 0, rate = 0;
    readRegs(0x80, &threshold, 1);
    readRegs(0x88, &rate, 1);
    Serial.printf("touch: chip found, id 0x%02X, threshold %u, rate %u\n", id, threshold, rate);
    return;
  }
  Serial.printf("touch: no answer at 0x%02X (SDA=%d, SCL=%d). Devices found:", FT6336_ADDR,
                PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", addr);
  }
  Serial.println();
}

static bool wasDown = false;

// Polls the chip over I2C rather than using the INT pin: on this board INT
// only pulses briefly, so it cannot tell whether a finger is down.
bool touchGetTap(uint16_t &x, uint16_t &y) {
  uint8_t status;
  if (!readRegs(REG_TD_STATUS, &status, 1)) return false;
  uint8_t fingers = status & 0x0F;
  bool down = fingers > 0 && fingers <= 2;  // the chip tracks at most 2 points
  if (!down) {
    wasDown = false;
    return false;
  }

  // A tap is the moment the screen goes from untouched to touched; staying
  // down (or the finger count changing) does not count again.
  bool isNewPress = !wasDown;
  wasDown = true;
  if (!isNewPress) return false;

  uint8_t p[4];
  if (!readRegs(REG_P1_XH, p, 4)) return false;
  x = ((p[0] & 0x0F) << 8) | p[1];
  y = ((p[2] & 0x0F) << 8) | p[3];
  return true;
}
