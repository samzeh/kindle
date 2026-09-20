#include "hal/Touch.h"

#include <Wire.h>

namespace {

constexpr uint8_t FT6336_ADDR = 0x38;

constexpr uint8_t REG_DEVICE_MODE = 0x00;
constexpr uint8_t REG_TD_STATUS = 0x02;
constexpr uint8_t REG_THGROUP = 0x80;
constexpr uint8_t REG_PERIODACTIVE = 0x88;
constexpr uint8_t REG_G_MODE = 0xA4;
constexpr uint8_t REG_PWR_MODE = 0xA5;
constexpr uint8_t REG_FIRMWARE_ID = 0xA6;
constexpr uint8_t REG_VENDOR_ID = 0xA8;

constexpr uint8_t PWR_MODE_MONITOR = 0x01;

// A contact must last this long to count as a long press rather than a tap.
constexpr uint32_t LONG_PRESS_MS = 600;
// Travel beyond this many pixels makes it a swipe, not a tap.
constexpr int16_t TAP_SLOP_PX = 24;
constexpr int16_t SWIPE_MIN_PX = 60;

}  // namespace

Touch gTouch;

bool Touch::begin() {
  pinMode(PIN_TOUCH_INT, INPUT);  // GPIO36 is input-only, pull-up is on-board

  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);  // FT6336U needs ~300ms after reset before it answers

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  uint8_t vendor = 0;
  if (!readRegs(REG_VENDOR_ID, &vendor, 1)) {
    log_e("FT6336U did not respond on I2C");
    present_ = false;
    return false;
  }

  uint8_t firmware = 0;
  readRegs(REG_FIRMWARE_ID, &firmware, 1);
  log_i("FT6336U vendor=0x%02X firmware=0x%02X", vendor, firmware);

  writeReg(REG_DEVICE_MODE, 0x00);  // normal reporting
  writeReg(REG_G_MODE, 0x00);       // INT stays low for the whole contact
  writeReg(REG_THGROUP, 22);        // touch threshold
  writeReg(REG_PERIODACTIVE, 12);   // ~80Hz report rate while active

  present_ = true;
  lastActivity_ = millis();
  return true;
}

bool Touch::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t got = Wire.requestFrom(static_cast<uint8_t>(FT6336_ADDR), len);
  if (got != len) return false;

  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool Touch::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Touch::readPoint(int16_t& x, int16_t& y) {
  // 0x02 TD_STATUS, then 0x03..0x06 = P1 XH, XL, YH, YL.
  uint8_t buf[5];
  if (!readRegs(REG_TD_STATUS, buf, sizeof(buf))) return false;

  uint8_t points = buf[0] & 0x0F;
  if (points == 0 || points > 2) return false;

  int16_t rawX = ((buf[1] & 0x0F) << 8) | buf[2];
  int16_t rawY = ((buf[3] & 0x0F) << 8) | buf[4];

  mapToScreen(rawX, rawY, x, y);
  return true;
}

void Touch::mapToScreen(int16_t rawX, int16_t rawY, int16_t& outX, int16_t& outY) {
  int16_t x = rawX;
  int16_t y = rawY;

  if (TOUCH_INVERT_X) x = TOUCH_NATIVE_W - 1 - x;
  if (TOUCH_INVERT_Y) y = TOUCH_NATIVE_H - 1 - y;

  if (TOUCH_SWAP_XY) {
    outX = y;
    outY = x;
  } else {
    outX = x;
    outY = y;
  }

  outX = constrain(outX, static_cast<int16_t>(0), static_cast<int16_t>(SCREEN_W - 1));
  outY = constrain(outY, static_cast<int16_t>(0), static_cast<int16_t>(SCREEN_H - 1));
}

TouchEvent Touch::poll() {
  TouchEvent ev;
  if (!present_) return ev;

  bool touching = contact();

  if (touching) {
    int16_t x, y;
    if (readPoint(x, y)) {
      lastX_ = x;
      lastY_ = y;
      if (!down_) {
        down_ = true;
        downX_ = x;
        downY_ = y;
        downMs_ = millis();
      }
      lastActivity_ = millis();
    }
    return ev;
  }

  if (!down_) return ev;

  // Finger lifted: classify what happened.
  down_ = false;
  lastActivity_ = millis();

  uint32_t heldMs = millis() - downMs_;
  int16_t dx = lastX_ - downX_;
  int16_t dy = lastY_ - downY_;
  int16_t adx = abs(dx);
  int16_t ady = abs(dy);

  ev.x = downX_;
  ev.y = downY_;

  if (adx < TAP_SLOP_PX && ady < TAP_SLOP_PX) {
    ev.gesture = heldMs >= LONG_PRESS_MS ? Gesture::LongPress : Gesture::Tap;
  } else if (adx > ady && adx >= SWIPE_MIN_PX) {
    ev.gesture = dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
  } else if (ady >= SWIPE_MIN_PX) {
    ev.gesture = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  } else {
    ev.gesture = Gesture::Tap;
  }

  return ev;
}

void Touch::sleep() {
  if (!present_) return;
  writeReg(REG_PWR_MODE, PWR_MODE_MONITOR);
}
