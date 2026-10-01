// Every hardware-specific setting lives here. When moving to a different
// board or MCU (e.g. a custom PCB), this should be the only file to change.
#pragma once

// ---- E-paper (GDEQ0426T82, SSD1677 controller) on hardware SPI ----
#define PIN_EPD_SCK   18
#define PIN_EPD_MOSI  23
#define PIN_EPD_CS    5
#define PIN_EPD_DC    17
#define PIN_EPD_RST   16
#define PIN_EPD_BUSY  4

// ---- Touch (FT6336) on I2C ----
#define PIN_TOUCH_SDA 32
#define PIN_TOUCH_SCL 33
#define PIN_TOUCH_RST 26
#define PIN_TOUCH_INT 36  // input-only pin, active low while touched

// ---- Front light ----
#define PIN_FRONTLIGHT 19

// ---- microSD card, on its own SPI bus (HSPI) ----
// Not shared with the screen, whose pins are driven directly by epd.cpp.
// Avoid GPIO12 (a boot strapping pin: a card pulling it high stops the
// ESP32 booting) and GPIO19 (the front light).
#define PIN_SD_SCK  14
#define PIN_SD_MOSI 13
#define PIN_SD_MISO 27
#define PIN_SD_CS   25

// ---- Screen orientation ----
// Set to 1 if the image comes out mirrored (this panel's rows run in the
// opposite direction to how images are stored in memory).
#define EPD_FLIP_ROWS 1

// 1 or 3 = portrait (480 x 800). If the page appears upside down, swap them.
#define SCREEN_ROTATION 3

// The touch panel reports x across the short (480 px) edge. If tapping the
// right side of the screen goes back instead of forward, set this to 1.
#define TOUCH_FLIP_X 0
#define TOUCH_WIDTH  480
