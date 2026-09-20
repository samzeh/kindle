// Phase 0 bring-up diagnostics.
//
// This is a separate firmware from the reader (`pio run -e bringup -t upload`)
// whose only job is to answer the hardware questions the reader firmware has to
// assume: which GPIO the FTS02 wires to the microSD chip select, which way the
// touch panel's axes run relative to the rotated display, and whether the
// panel's refresh timing looks right for the P3 booster setting.
//
// Each test is independent and rerunnable from a single-key serial menu, so a
// failure can be re-tested after moving one jumper without a reflash.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

#include "config.h"
#include "hal/Display.h"
#include "text/FontCache.h"
#include "ui/Draw.h"

namespace {

constexpr uint8_t FT6336_ADDR = 0x38;
constexpr uint8_t REG_TD_STATUS = 0x02;
constexpr uint8_t REG_G_MODE = 0xA4;
constexpr uint8_t REG_DEVICE_MODE = 0x00;
constexpr uint8_t REG_THGROUP = 0x80;
constexpr uint8_t REG_PERIODACTIVE = 0x88;
constexpr uint8_t REG_FIRMWARE_ID = 0xA6;
constexpr uint8_t REG_VENDOR_ID = 0xA8;

// Every GPIO on the WROOM that is free once the e-paper (4, 5, 16-19, 23) and
// the touch panel (26, 32, 33, 36) have their pins, minus the strapping pins
// (0, 2, 12) and the input-only pins (34, 35, 39) that cannot drive a chip
// select. The config.h guess goes first so a correct guess exits immediately.
constexpr int kSdCsCandidates[] = {PIN_SD_CS, 13, 14, 25, 27, 21, 22};

// Results accumulate across tests so the summary can print a config.h diff.
struct Findings {
  bool epdRan = false;
  uint32_t fullRefreshMs = 0;
  uint32_t partialRefreshMs = 0;

  bool touchFound = false;
  uint8_t touchVendor = 0;
  uint8_t touchFirmware = 0;
  bool touchCalibrated = false;
  bool swapXy = false;
  bool invertX = false;
  bool invertY = false;

  bool sdFound = false;
  int sdCs = -1;
  uint64_t sdSizeMb = 0;
  uint32_t sdWriteKbps = 0;
  uint32_t sdReadKbps = 0;
};

Findings gFindings;

// --- serial helpers -------------------------------------------------------

void rule(const char* title) {
  Serial.println();
  Serial.printf("======== %s ========\n", title);
}

// Blocks until a key arrives, so a test can wait on the human. Returns 0 if
// the wait times out.
char waitKey(uint32_t timeoutMs = 60000) {
  uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c != '\r' && c != '\n') return c;
    }
    delay(5);
  }
  return 0;
}

// --- raw touch ------------------------------------------------------------
//
// The reader's Touch class maps coordinates through the config.h flags, which
// is exactly what we are trying to determine here, so bring-up talks to the
// controller directly and reports the untranslated values.

bool touchRead(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(FT6336_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void touchWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(FT6336_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

bool touchRawPoint(int16_t& x, int16_t& y) {
  uint8_t buf[5];
  if (!touchRead(REG_TD_STATUS, buf, sizeof(buf))) return false;
  uint8_t points = buf[0] & 0x0F;
  if (points == 0 || points > 2) return false;
  x = ((buf[1] & 0x0F) << 8) | buf[2];
  y = ((buf[3] & 0x0F) << 8) | buf[4];
  return true;
}

// Waits for a full press-and-release and reports where the finger went down,
// which is steadier than sampling mid-contact.
bool awaitTap(int16_t& x, int16_t& y, uint32_t timeoutMs = 30000) {
  uint32_t deadline = millis() + timeoutMs;
  bool down = false;
  int16_t firstX = 0, firstY = 0;

  while (millis() < deadline) {
    int16_t rx, ry;
    if (touchRawPoint(rx, ry)) {
      if (!down) {
        down = true;
        firstX = rx;
        firstY = ry;
      }
    } else if (down) {
      x = firstX;
      y = firstY;
      return true;
    }
    delay(10);
  }
  return false;
}

// --- test 1: e-paper ------------------------------------------------------

void drawTestPattern() {
  EpdPanel& g = gDisplay.gfx();
  gDisplay.clearBuffer();

  // A 1px border proves no rows or columns are being dropped at the edges.
  ui::drawRect(0, 0, SCREEN_W, SCREEN_H);
  ui::drawRect(2, 2, SCREEN_W - 4, SCREEN_H - 4);

  // Solid blocks: contrast depends on the P3 booster setting, so a washed-out
  // black here is the first sign P3 is wrong for this panel.
  ui::fillRect(24, 24, 120, 80, true);
  for (int16_t y = 24; y < 104; y++) {           // 50% checker
    for (int16_t x = 160; x < 280; x++) {
      if (((x + y) & 1) == 0) g.drawPixel(x, y, GxEPD_BLACK);
    }
  }
  for (int16_t x = 296; x < 456; x += 2) {       // 1px vertical lines
    g.drawFastVLine(x, 24, 80, GxEPD_BLACK);
  }

  ui::drawTextCentered(SCREEN_W / 2, 150, "e-paper bring-up", FontStyle::Bold, 30);

  // Every selectable body size, to confirm the flash-resident font rasterises
  // and that small sizes stay legible at 219 DPI.
  int16_t y = 200;
  for (uint8_t i = 0; i < FONT_SIZE_COUNT; i++) {
    uint16_t px = FONT_SIZES[i];
    char line[64];
    snprintf(line, sizeof(line), "%upx  Hamburgefonstiv 0123", px);
    ui::drawText(24, y, line, FontStyle::Regular, px);
    y += px + 14;
  }

  // Corner markers: if any is clipped, rotation or geometry is wrong.
  const int16_t m = 20;
  g.drawFastHLine(0, 0, m, GxEPD_BLACK);
  g.drawFastVLine(0, 0, m, GxEPD_BLACK);
  g.drawFastHLine(SCREEN_W - m, SCREEN_H - 1, m, GxEPD_BLACK);
  g.drawFastVLine(SCREEN_W - 1, SCREEN_H - m, m, GxEPD_BLACK);

  ui::drawTextCentered(SCREEN_W / 2, SCREEN_H - 40,
                       "all 4 corners visible?", FontStyle::Italic, 23);
}

void testEpd() {
  rule("1. e-paper panel");
  Serial.println("Full refresh (expect a flash, then ~3.5s)...");

  uint32_t t0 = millis();
  gDisplay.clearScreen();
  gFindings.fullRefreshMs = millis() - t0;
  Serial.printf("  full refresh: %lu ms\n", (unsigned long)gFindings.fullRefreshMs);

  drawTestPattern();
  t0 = millis();
  gDisplay.present(Display::Refresh::Partial);
  gFindings.partialRefreshMs = millis() - t0;
  Serial.printf("  partial refresh: %lu ms (plan target ~420 ms)\n",
                (unsigned long)gFindings.partialRefreshMs);

  gFindings.epdRan = true;

  Serial.println("Check the panel:");
  Serial.println("  - border and all 4 corner ticks present");
  Serial.println("  - black block is solid black, not grey (grey => check P3)");
  Serial.println("  - the 1px line block is crisp, not smeared");
  Serial.println("  - every font size is legible");
  if (gFindings.partialRefreshMs > 1500) {
    Serial.println("  ! partial refresh was slow; BUSY may be misread or SPI slow");
  }
}

// --- test 2: I2C / touch presence ----------------------------------------

void testI2c() {
  rule("2. touch controller on I2C");

  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);  // FT6336U is not addressable for ~300ms after reset

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  Serial.printf("Scanning SDA=%d SCL=%d ...\n", PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X%s\n", addr,
                    addr == FT6336_ADDR ? "  <- FT6336U" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  no I2C devices.");
    Serial.println("  - is the 6-pin touch FPC fully seated and latched?");
    Serial.println("  - SDA/SCL swapped? try PIN_TOUCH_SDA/SCL swapped in config.h");
    Serial.println("  - the panel needs power for the controller to answer");
    return;
  }

  if (!touchRead(REG_VENDOR_ID, &gFindings.touchVendor, 1)) {
    Serial.println("  0x38 ACKed but register read failed.");
    return;
  }
  touchRead(REG_FIRMWARE_ID, &gFindings.touchFirmware, 1);
  Serial.printf("  vendor=0x%02X firmware=0x%02X (vendor 0x11 is typical)\n",
                gFindings.touchVendor, gFindings.touchFirmware);

  touchWrite(REG_DEVICE_MODE, 0x00);
  touchWrite(REG_G_MODE, 0x00);
  touchWrite(REG_THGROUP, 22);
  touchWrite(REG_PERIODACTIVE, 12);
  gFindings.touchFound = true;
}

// --- test 3: touch axis calibration --------------------------------------

void promptTarget(const char* label, int16_t x, int16_t y) {
  EpdPanel& g = gDisplay.gfx();
  gDisplay.clearBuffer();
  ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 - 20, "tap the circle",
                       FontStyle::Bold, 30);
  ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 + 20, label,
                       FontStyle::Regular, 26);
  g.fillCircle(x, y, 14, GxEPD_BLACK);
  g.drawCircle(x, y, 26, GxEPD_BLACK);
  gDisplay.present(Display::Refresh::Partial);
}

void testTouchAxes() {
  rule("3. touch axis calibration");
  if (!gFindings.touchFound) {
    Serial.println("Skipped: run test 2 first and get the controller answering.");
    return;
  }

  // Three taps are enough: two along the screen's x axis to find which raw
  // axis tracks it and in which direction, one along y for the remaining sign.
  struct Sample {
    const char* label;
    int16_t sx, sy;
    int16_t rx, ry;
  } s[3] = {
      {"top-left", 50, 50, 0, 0},
      {"top-right", (int16_t)(SCREEN_W - 50), 50, 0, 0},
      {"bottom-left", 50, (int16_t)(SCREEN_H - 50), 0, 0},
  };

  for (auto& p : s) {
    promptTarget(p.label, p.sx, p.sy);
    Serial.printf("Tap the %s circle...\n", p.label);
    if (!awaitTap(p.rx, p.ry)) {
      Serial.println("  timed out waiting for a tap.");
      return;
    }
    Serial.printf("  screen (%d,%d) -> raw (%d,%d)\n", p.sx, p.sy, p.rx, p.ry);
    delay(400);  // let the finger clear before the next prompt
  }

  // Which raw axis moved when we walked across the screen horizontally?
  int32_t dxAlongScreenX = s[1].rx - s[0].rx;
  int32_t dyAlongScreenX = s[1].ry - s[0].ry;
  int32_t dxAlongScreenY = s[2].rx - s[0].rx;
  int32_t dyAlongScreenY = s[2].ry - s[0].ry;

  if (abs(dxAlongScreenX) >= abs(dyAlongScreenX)) {
    // Raw x tracks screen x: no swap, each sign read off its own axis.
    gFindings.swapXy = false;
    gFindings.invertX = dxAlongScreenX < 0;
    gFindings.invertY = dyAlongScreenY < 0;
  } else {
    // Raw y tracks screen x. mapToScreen() inverts before swapping, so the
    // INVERT_Y flag governs the output x and INVERT_X governs the output y.
    gFindings.swapXy = true;
    gFindings.invertY = dyAlongScreenX < 0;
    gFindings.invertX = dxAlongScreenY < 0;
  }
  gFindings.touchCalibrated = true;

  Serial.println("Derived config.h flags:");
  Serial.printf("  TOUCH_SWAP_XY   = %s\n", gFindings.swapXy ? "true" : "false");
  Serial.printf("  TOUCH_INVERT_X  = %s\n", gFindings.invertX ? "true" : "false");
  Serial.printf("  TOUCH_INVERT_Y  = %s\n", gFindings.invertY ? "true" : "false");
  if (gFindings.swapXy) {
    Serial.printf("  TOUCH_NATIVE_W  = %d   // swapped: raw axes are transposed\n",
                  SCREEN_H);
    Serial.printf("  TOUCH_NATIVE_H  = %d\n", SCREEN_W);
  }
  Serial.println("Then rerun test 4 to confirm taps land where you expect.");
}

// --- test 4: live touch zones -------------------------------------------

void drawZones() {
  gDisplay.clearBuffer();
  int16_t third = SCREEN_W / 3;
  ui::drawRect(0, 0, SCREEN_W, SCREEN_H);
  gDisplay.gfx().drawFastVLine(third, 0, SCREEN_H, GxEPD_BLACK);
  gDisplay.gfx().drawFastVLine(third * 2, 0, SCREEN_H, GxEPD_BLACK);
  gDisplay.gfx().drawFastHLine(0, 120, SCREEN_W, GxEPD_BLACK);

  ui::drawTextCentered(SCREEN_W / 2, 70, "MENU", FontStyle::Bold, 26);
  ui::drawTextCentered(third / 2, SCREEN_H / 2, "PREV", FontStyle::Bold, 26);
  ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2, "-", FontStyle::Bold, 26);
  ui::drawTextCentered(third * 5 / 2, SCREEN_H / 2, "NEXT", FontStyle::Bold, 26);
  ui::drawTextCentered(SCREEN_W / 2, SCREEN_H - 40, "tap zones; any key to stop",
                       FontStyle::Italic, 23);
  gDisplay.present(Display::Refresh::Partial);
}

void testTouchZones() {
  rule("4. live touch zones");
  if (!gFindings.touchFound) {
    Serial.println("Skipped: no touch controller.");
    return;
  }

  drawZones();
  Serial.println("Tapping inverts the zone you hit. Any key to stop.");

  while (!Serial.available()) {
    int16_t rx, ry;
    if (!awaitTap(rx, ry, 500)) continue;

    // Apply the flags we derived (or the ones already in config.h).
    bool swapXy = gFindings.touchCalibrated ? gFindings.swapXy : TOUCH_SWAP_XY;
    bool invX = gFindings.touchCalibrated ? gFindings.invertX : TOUCH_INVERT_X;
    bool invY = gFindings.touchCalibrated ? gFindings.invertY : TOUCH_INVERT_Y;

    int16_t nw = swapXy ? SCREEN_H : TOUCH_NATIVE_W;
    int16_t nh = swapXy ? SCREEN_W : TOUCH_NATIVE_H;
    int16_t mx = invX ? nw - 1 - rx : rx;
    int16_t my = invY ? nh - 1 - ry : ry;
    int16_t x = swapXy ? my : mx;
    int16_t y = swapXy ? mx : my;
    x = constrain(x, (int16_t)0, (int16_t)(SCREEN_W - 1));
    y = constrain(y, (int16_t)0, (int16_t)(SCREEN_H - 1));

    int16_t third = SCREEN_W / 3;
    const char* zone;
    int16_t zx, zy, zw, zh;
    if (y < 120) {
      zone = "MENU";
      zx = 0; zy = 0; zw = SCREEN_W; zh = 120;
    } else if (x < third) {
      zone = "PREV";
      zx = 0; zy = 120; zw = third; zh = SCREEN_H - 120;
    } else if (x >= third * 2) {
      zone = "NEXT";
      zx = third * 2; zy = 120; zw = SCREEN_W - third * 2; zh = SCREEN_H - 120;
    } else {
      zone = "middle";
      zx = third; zy = 120; zw = third; zh = SCREEN_H - 120;
    }
    Serial.printf("  raw (%d,%d) -> screen (%d,%d) -> %s\n", rx, ry, x, y, zone);

    // Invert just that zone: a partial-window refresh so the feedback is fast.
    EpdPanel& g = gDisplay.gfx();
    for (int16_t py = zy; py < zy + zh; py++) {
      for (int16_t px = zx; px < zx + zw; px++) {
        g.drawPixel(px, py, GxEPD_BLACK);
      }
    }
    gDisplay.presentRegion(zx, zy, zw, zh);
    delay(250);
    drawZones();
  }
  while (Serial.available()) Serial.read();
}

// --- test 5: SD chip-select probe ---------------------------------------

bool sdSpeedTest(uint32_t& writeKbps, uint32_t& readKbps) {
  // 64KB is enough to swamp the per-call overhead without being slow.
  constexpr size_t kChunk = 2048;
  constexpr int kChunks = 32;
  static uint8_t buf[kChunk];
  for (size_t i = 0; i < kChunk; i++) buf[i] = (uint8_t)i;

  File f = SD.open("/bringup.bin", FILE_WRITE);
  if (!f) {
    Serial.println("  could not open /bringup.bin for write (card write-locked?)");
    return false;
  }
  uint32_t t0 = millis();
  for (int i = 0; i < kChunks; i++) {
    if (f.write(buf, kChunk) != kChunk) {
      Serial.println("  short write");
      f.close();
      return false;
    }
  }
  f.flush();
  uint32_t writeMs = millis() - t0;
  f.close();

  f = SD.open("/bringup.bin", FILE_READ);
  if (!f) return false;
  t0 = millis();
  size_t total = 0;
  bool intact = true;
  while (f.available()) {
    size_t n = f.read(buf, kChunk);
    if (n == 0) break;
    for (size_t i = 0; i < n; i++) {
      if (buf[i] != (uint8_t)((total + i) % kChunk)) intact = false;
    }
    total += n;
  }
  uint32_t readMs = millis() - t0;
  f.close();
  SD.remove("/bringup.bin");

  if (!intact) {
    Serial.println("  ! data read back did not match; bus is marginal");
    Serial.println("    try lowering SD_FREQ_HZ in src/hal/Storage.cpp");
  }
  writeKbps = writeMs ? (kChunk * kChunks) / writeMs : 0;
  readKbps = readMs ? total / readMs : 0;
  return intact;
}

void testSd() {
  rule("5. microSD chip-select probe");

  // Park the panel's chip select high so probe traffic cannot reach it.
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, HIGH);

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  Serial.println("Trying each free GPIO as SD_CS (brief, harmless pulses)...");
  for (int cs : kSdCsCandidates) {
    Serial.printf("  GPIO %-2d ... ", cs);
    SD.end();
    delay(20);
    if (!SD.begin(cs, SPI, 4000000, "/sd", 8)) {
      Serial.println("no card");
      continue;
    }

    uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
      Serial.println("mounted but CARD_NONE");
      SD.end();
      continue;
    }

    const char* typeName = type == CARD_MMC   ? "MMC"
                           : type == CARD_SD  ? "SDSC"
                           : type == CARD_SDHC ? "SDHC"
                                               : "unknown";
    gFindings.sdFound = true;
    gFindings.sdCs = cs;
    gFindings.sdSizeMb = SD.cardSize() / (1024ULL * 1024ULL);
    Serial.printf("MOUNTED  %s, %llu MB\n", typeName, gFindings.sdSizeMb);

    Serial.println("  root:");
    File root = SD.open("/");
    if (root) {
      for (File e = root.openNextFile(); e; e = root.openNextFile()) {
        Serial.printf("    %-28s %s%u\n", e.name(), e.isDirectory() ? "<dir> " : "",
                      (unsigned)e.size());
        e.close();
      }
      root.close();
    }

    Serial.println("  throughput:");
    if (sdSpeedTest(gFindings.sdWriteKbps, gFindings.sdReadKbps)) {
      Serial.printf("    write %lu KB/s, read %lu KB/s\n",
                    (unsigned long)gFindings.sdWriteKbps,
                    (unsigned long)gFindings.sdReadKbps);
    }
    break;
  }

  if (!gFindings.sdFound) {
    Serial.println("No card found on any candidate pin.");
    Serial.println("  - is the card inserted fully and FAT32 (not exFAT)?");
    Serial.println("  - MISO on GPIO 19 is required; the vendor demo left it");
    Serial.println("    driven as a front-light pin, which would break this");
    Serial.println("  - the FTS02 may not break CS out at all: then wire the U3");
    Serial.println("    slot's CS pad to a free GPIO and add it to kSdCsCandidates");
    return;
  }

  if (gFindings.sdCs != PIN_SD_CS) {
    Serial.printf("\n  >> Set PIN_SD_CS = %d in src/config.h (it currently says %d)\n",
                  gFindings.sdCs, PIN_SD_CS);
  } else {
    Serial.printf("\n  >> PIN_SD_CS = %d in config.h is correct.\n", PIN_SD_CS);
  }
}

// --- test 6: summary ----------------------------------------------------

void testSummary() {
  rule("6. summary");
  Serial.printf("chip           : %s rev%d, %d cores @ %lu MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned long)getCpuFrequencyMhz());
  Serial.printf("flash          : %lu MB\n",
                (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("psram          : %lu bytes (0 is expected on WROOM-32)\n",
                (unsigned long)ESP.getPsramSize());
  Serial.printf("heap free      : %lu, largest block %lu\n",
                (unsigned long)ESP.getFreeHeap(),
                (unsigned long)ESP.getMaxAllocHeap());

  Serial.println();
  Serial.printf("e-paper        : %s", gFindings.epdRan ? "ran" : "NOT TESTED");
  if (gFindings.epdRan) {
    Serial.printf(" (full %lu ms, partial %lu ms)",
                  (unsigned long)gFindings.fullRefreshMs,
                  (unsigned long)gFindings.partialRefreshMs);
  }
  Serial.println();

  Serial.printf("touch          : %s",
                gFindings.touchFound ? "found at 0x38" : "NOT FOUND");
  if (gFindings.touchFound) {
    Serial.printf(" (vendor 0x%02X fw 0x%02X)", gFindings.touchVendor,
                  gFindings.touchFirmware);
  }
  Serial.println();

  Serial.printf("microSD        : ");
  if (gFindings.sdFound) {
    Serial.printf("CS=GPIO %d, %llu MB\n", gFindings.sdCs, gFindings.sdSizeMb);
  } else {
    Serial.println("NOT FOUND");
  }

  Serial.println();
  Serial.println("--- config.h should say ---");
  Serial.printf("  PIN_SD_CS       = %d\n",
                gFindings.sdFound ? gFindings.sdCs : PIN_SD_CS);
  Serial.printf("  TOUCH_SWAP_XY   = %s\n",
                (gFindings.touchCalibrated ? gFindings.swapXy : TOUCH_SWAP_XY)
                    ? "true" : "false");
  Serial.printf("  TOUCH_INVERT_X  = %s\n",
                (gFindings.touchCalibrated ? gFindings.invertX : TOUCH_INVERT_X)
                    ? "true" : "false");
  Serial.printf("  TOUCH_INVERT_Y  = %s\n",
                (gFindings.touchCalibrated ? gFindings.invertY : TOUCH_INVERT_Y)
                    ? "true" : "false");
  if (!gFindings.touchCalibrated) {
    Serial.println("  (touch flags unverified -- run test 3)");
  }
}

void menu() {
  Serial.println();
  Serial.println("--- bring-up menu ---");
  Serial.println(" 1  e-paper: refresh timing + test pattern");
  Serial.println(" 2  touch: I2C scan and controller ID");
  Serial.println(" 3  touch: axis calibration (3 taps)");
  Serial.println(" 4  touch: live zone test");
  Serial.println(" 5  microSD: probe chip select, list, speed test");
  Serial.println(" 6  summary + config.h values");
  Serial.println(" a  run 1,2,3,5,6 in order");
  Serial.print("> ");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("ESP32 e-reader bring-up diagnostics");
  Serial.printf("reset reason %d\n", (int)esp_reset_reason());

  gDisplay.begin();
  if (!gFonts.begin()) {
    Serial.println("FATAL: embedded font failed to load; on-screen tests will be blank");
  }

  menu();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }
  char c = Serial.read();
  if (c == '\r' || c == '\n') return;

  switch (c) {
    case '1': testEpd(); break;
    case '2': testI2c(); break;
    case '3': testTouchAxes(); break;
    case '4': testTouchZones(); break;
    case '5': testSd(); break;
    case '6': testSummary(); break;
    case 'a':
      testEpd();
      testI2c();
      testTouchAxes();
      testSd();
      testSummary();
      break;
    default:
      Serial.printf("unknown key '%c'\n", c);
      break;
  }
  menu();
}
