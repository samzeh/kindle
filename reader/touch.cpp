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

void touchBegin() {
  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);  // FT6336 needs ~300 ms after reset before it answers on I2C
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
}

bool touchGetTap(uint16_t &x, uint16_t &y) {
  static bool wasDown = false;

  if (digitalRead(PIN_TOUCH_INT) == HIGH) {  // no finger on the panel
    wasDown = false;
    return false;
  }

  uint8_t status;
  if (!readRegs(REG_TD_STATUS, &status, 1)) return false;
  bool down = (status & 0x0F) > 0;
  bool isNewPress = down && !wasDown;
  wasDown = down;
  if (!isNewPress) return false;

  uint8_t p[4];
  if (!readRegs(REG_P1_XH, p, 4)) return false;
  x = ((p[0] & 0x0F) << 8) | p[1];
  y = ((p[2] & 0x0F) << 8) | p[3];
  return true;
}
