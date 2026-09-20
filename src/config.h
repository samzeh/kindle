#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pin map: ESP-WROOM-32 DevKit wired to the Good Display ESP32-FTS02 adapter.
//
// The e-paper and the SD card share VSPI with separate chip selects. Both
// GxEPD2 and the Arduino SD library wrap transfers in SPI transactions, so
// sharing the bus is safe.
//
// Board setup that is not controlled from here:
//
//   P3 DIP switch  selects the e-paper booster resistor. Confirm the setting
//                  for GDEQ0426T82 in the FTS02 manual before driving the
//                  panel; the wrong value gives weak or uneven contrast.
//
//   Front light    is powered from the adapter's own Type-C input at P0 and
//                  dimmed by the R14 trim pot, not by any GPIO. The panel is
//                  rated <=15V / <=15mA, but the FTS02 manual lists 21V for
//                  4.2" front lights and calls 15V "reserved". Start with R14
//                  at minimum and measure before increasing, or the LED
//                  string can be overdriven.
//
//   GPIO 19        was the vendor demo's inert front light enable. It does
//                  nothing on this adapter and is reused below for SD MISO.
// ---------------------------------------------------------------------------

// E-paper: GDEQ0426T82-FT01C, SSD1677 controller.
static constexpr int PIN_EPD_BUSY = 4;
static constexpr int PIN_EPD_RST = 16;
static constexpr int PIN_EPD_DC = 17;
static constexpr int PIN_EPD_CS = 5;

// Shared VSPI.
static constexpr int PIN_SPI_SCK = 18;
static constexpr int PIN_SPI_MOSI = 23;
static constexpr int PIN_SPI_MISO = 19;

// microSD in the FTS02's U3 slot.
//
// UNVERIFIED: this chip select was not confirmed against the FTS02 schematic
// (ESP32-FTS02_SCH-20250819 on good-display.cn). If the card never mounts,
// this is the first thing to change. GPIO 25 is the other free candidate.
// Avoid GPIO 0, 2 and 12, which are strapping pins.
static constexpr int PIN_SD_CS = 15;

// FT6336U capacitive touch, on hardware I2C rather than the vendor's bit-bang.
static constexpr int PIN_TOUCH_SDA = 32;
static constexpr int PIN_TOUCH_SCL = 33;
static constexpr int PIN_TOUCH_RST = 26;
static constexpr int PIN_TOUCH_INT = 36;  // RTC-capable, used for ext0 wake

// ---------------------------------------------------------------------------
// Display geometry
// ---------------------------------------------------------------------------

// The panel is natively 800x480 (source x gate). Rotation 1 gives a 480x800
// portrait page, which at 219 DPI is a 2.19" x 3.65" reading column.
static constexpr uint8_t DISPLAY_ROTATION = 1;
static constexpr int16_t SCREEN_W = 480;
static constexpr int16_t SCREEN_H = 800;

// Touch reports native portrait coordinates (x 0..479, y 0..799), matching
// rotation 1 directly. Flip these if the axes come out mirrored on hardware.
static constexpr bool TOUCH_SWAP_XY = false;
static constexpr bool TOUCH_INVERT_X = false;
static constexpr bool TOUCH_INVERT_Y = false;
static constexpr int16_t TOUCH_NATIVE_W = 480;
static constexpr int16_t TOUCH_NATIVE_H = 800;

// ---------------------------------------------------------------------------
// Reading page layout
// ---------------------------------------------------------------------------

static constexpr int16_t STATUS_BAR_H = 28;

// Selectable body sizes in pixels. At 219 DPI a 26px em is roughly a 8.5pt
// print size and fits ~36 characters per line.
static constexpr uint16_t FONT_SIZES[] = {20, 23, 26, 30, 34, 38};
static constexpr uint8_t FONT_SIZE_COUNT = sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]);
static constexpr uint8_t FONT_SIZE_DEFAULT = 2;

static constexpr uint16_t MARGIN_CHOICES[] = {16, 24, 32, 44};
static constexpr uint8_t MARGIN_COUNT = sizeof(MARGIN_CHOICES) / sizeof(MARGIN_CHOICES[0]);
static constexpr uint8_t MARGIN_DEFAULT = 2;

// Line advance as a percentage of the font's natural line height.
static constexpr uint8_t LINE_SPACING_DEFAULT = 118;

// Force a flashing full refresh after this many partial refreshes, to clear
// accumulated ghosting.
static constexpr uint16_t FULL_REFRESH_INTERVAL = 8;

// ---------------------------------------------------------------------------
// Storage layout on the SD card
// ---------------------------------------------------------------------------

static constexpr const char* DIR_BOOKS = "/books";
static constexpr const char* DIR_CACHE = "/cache";

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------

static constexpr uint32_t IDLE_SLEEP_MS = 120000;  // deep sleep after 2 min idle
