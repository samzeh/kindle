#!/bin/sh
# Downloads the GNU FreeFont outlines into tools/freefont/, which is where
# make_fonts.sh expects them. They are not committed: the zip is 5.8 MB and
# only needed when regenerating the font headers in reader/fonts/.
#
#   sh tools/fetch_fonts.sh
#
# These are the same outlines Adafruit generated its FreeSerif headers from,
# so the typeface matches the stock GFX fonts exactly.
set -e
dir="$(cd "$(dirname "$0")" && pwd)/freefont"
if [ -f "$dir/FreeSerif.ttf" ]; then
  echo "have the fonts in $dir"
  exit 0
fi
mkdir -p "$dir"
zip="$dir/freefont.zip"
echo "downloading GNU FreeFont"
curl -sfL -o "$zip" "https://ftp.gnu.org/gnu/freefont/freefont-ttf-20120503.zip"
# The zip holds one directory; lift the .ttf files out of it.
unzip -o -q -j "$zip" "*.ttf" -d "$dir"
rm -f "$zip"
echo "fonts are in $dir"
