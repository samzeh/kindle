// Minimal stand-in for the Arduino core, so the reader code and Adafruit GFX
// compile on a desktop computer.
#pragma once
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef bool boolean;

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_pointer(addr) (*(void *const *)(addr))
#define radians(deg) ((deg) * M_PI / 180.0)
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif

inline unsigned long millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

class __FlashStringHelper;

class String {
public:
  const char *c_str() const { return ""; }
  unsigned length() const { return 0; }
};

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t write(const char *s) {
    size_t n = 0;
    while (*s) n += write((uint8_t)*s++);
    return n;
  }
  size_t print(const char *s) { return write(s); }
};

// Serial output goes to the terminal.
class SerialPort {
public:
  int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vprintf(format, args);
    va_end(args);
    fflush(stdout);
    return n;
  }
  size_t println(const char *s) { return (size_t)printf("%s\n", s); }
};
extern SerialPort Serial;
