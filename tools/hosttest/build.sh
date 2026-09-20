#!/usr/bin/env bash
# Compile and run the host-side tests for the hardware-independent code.
set -euo pipefail

cd "$(dirname "$0")/../.."
OUT=${OUT:-/tmp/kindle-hosttest}
mkdir -p "$OUT"

COMMON=(
  -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter
  -I tools/hosttest/shim -I tools/hosttest -I src -I lib/stb -I lib/miniz
)

SRC_CORE=(
  src/text/stb_impl.cpp
  src/text/FontCache.cpp
  src/text/Layout.cpp
  src/text/fonts/CharisSubset.cpp
)

echo "==> building test_layout"
g++ "${COMMON[@]}" "${SRC_CORE[@]}" tools/hosttest/test_layout.cpp -o "$OUT/test_layout"

echo "==> building test_epub"
g++ "${COMMON[@]}" "${SRC_CORE[@]}" \
  src/epub/XmlPull.cpp src/epub/XhtmlSource.cpp \
  tools/hosttest/test_epub.cpp -o "$OUT/test_epub"

echo "==> building test_import"
g++ "${COMMON[@]}" \
  -DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_ZLIB_APIS \
  -DMINIZ_NO_TIME \
  "${SRC_CORE[@]}" \
  src/epub/XmlPull.cpp src/epub/XhtmlSource.cpp src/epub/ZipReader.cpp \
  src/epub/Epub.cpp src/epub/PageIndex.cpp \
  lib/miniz/miniz.c \
  tools/hosttest/test_import.cpp -o "$OUT/test_import"

# A real EPUB on a directory that stands in for the SD card.
CARD="$OUT/card"
rm -rf "$CARD"
mkdir -p "$CARD/books" "$CARD/cache"
python3 tools/make_test_epub.py "$CARD/books/test.epub" --chapters 5 >/dev/null

echo
"$OUT/test_layout" "$OUT/page"
echo
"$OUT/test_epub"
echo
"$OUT/test_import" "$CARD"
