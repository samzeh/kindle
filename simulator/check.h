// The tests' tiny framework, shared by tests.cpp and test_epub.cpp.
#pragma once
#include <cstdio>
#include <cstring>

extern int failures;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      failures++;                                                         \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                        \
  do {                                                                    \
    long a_ = (long)(actual), e_ = (long)(expected);                      \
    if (a_ != e_) {                                                       \
      printf("FAIL %s:%d  %s: got %ld, want %ld\n", __FILE__, __LINE__,   \
             #actual, a_, e_);                                            \
      failures++;                                                         \
    }                                                                     \
  } while (0)

#define CHECK_STR(actual, expected)                                       \
  do {                                                                    \
    const char *a_ = (actual), *e_ = (expected);                          \
    if (strcmp(a_, e_) != 0) {                                            \
      printf("FAIL %s:%d  %s: got \"%s\", want \"%s\"\n", __FILE__,       \
             __LINE__, #actual, a_, e_);                                  \
      failures++;                                                         \
    }                                                                     \
  } while (0)
