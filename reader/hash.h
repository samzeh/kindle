// FNV-1a, a small fast 32-bit hash. Used to identify books and to look up
// files inside an EPUB without keeping every file name in memory.
#pragma once
#include <stddef.h>
#include <stdint.h>

static const uint32_t FNV_START = 2166136261u;

inline uint32_t fnv1a(const void *data, size_t n, uint32_t h = FNV_START) {
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

inline uint32_t fnv1a(const char *s, uint32_t h = FNV_START) {
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}
