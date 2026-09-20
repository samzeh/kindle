// Minimal Arduino shim so the layout, font and EPUB parsing code can be
// compiled and exercised on the host. Nothing hardware-specific lives in
// those translation units, which is the point of keeping them free of
// Display/Touch/SD dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using std::max;
using std::min;

template <typename T, typename L, typename H>
constexpr T constrain(T v, L lo, H hi) {
  return v < static_cast<T>(lo) ? static_cast<T>(lo)
                                : (v > static_cast<T>(hi) ? static_cast<T>(hi) : v);
}

#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)
#define log_d(fmt, ...) do { } while (0)
#define log_v(fmt, ...) do { } while (0)
