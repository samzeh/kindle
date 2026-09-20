#pragma once
#include <Arduino.h>

#include "text/FontCache.h"

// Text and chrome helpers for the interface, drawn with the same font cache
// the reader uses so the UI matches the page.
namespace ui {

int16_t textWidth(const char* s, FontStyle style, uint16_t px);

// Returns the pen x after the string.
int16_t drawText(int16_t x, int16_t baseline, const char* s, FontStyle style,
                 uint16_t px);

void drawTextCentered(int16_t centerX, int16_t baseline, const char* s,
                      FontStyle style, uint16_t px);

// Draws at most maxWidth pixels, appending an ellipsis when truncated.
void drawTextClipped(int16_t x, int16_t baseline, int16_t maxWidth, const char* s,
                     FontStyle style, uint16_t px);

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black);
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h);
void drawHLine(int16_t x, int16_t y, int16_t w);

// A tappable row with an optional right-aligned value; inverted when selected.
void drawListRow(int16_t y, int16_t h, const char* label, const char* value,
                 bool selected, uint16_t px);

// Full-screen message, used for errors and prompts.
void drawMessage(const char* title, const char* detail);

// Progress screen for import and pagination, which can take a while.
void drawProgress(const char* title, const char* stage, uint16_t done,
                  uint16_t total);

}  // namespace ui
