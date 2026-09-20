// The single translation unit that instantiates stb_truetype.
//
// Font data lives in flash .rodata, which the ESP32 memory-maps, so
// stb_truetype reads outlines in place without copying them into RAM.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#pragma GCC diagnostic pop
