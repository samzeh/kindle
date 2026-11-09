#!/bin/sh
# Regenerates every font header in reader/fonts/ from the GNU FreeFont
# outlines, using make_gfx_font.py.
#
#   sh tools/fetch_fonts.sh    # once: downloads the outlines
#   sh tools/make_fonts.sh
#
# The body text comes in the seven sizes the display settings offer, each in
# four faces (regular, italic, bold, bold italic). FreeSans at 9pt and 8pt is
# the settings sheet's own chrome, and FreeSerif8pt7b is the page strip.
#
# The generated headers are committed, so this only needs running when the
# sizes, the character range or the generator change.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
ttf="$root/tools/freefont"
out="$root/reader/fonts"

if [ ! -f "$ttf/FreeSerif.ttf" ]; then
  echo "no fonts in $ttf -- run: sh tools/fetch_fonts.sh" >&2
  exit 1
fi
mkdir -p "$out"

# Body text: the seven sizes of the font-size setting, four faces each.
for pt in 9 10 11 12 14 16 18; do
  for face in "" Bold Italic BoldItalic; do
    name="FreeSerif${face}${pt}pt7b"
    echo "  $name"
    python3 "$root/tools/make_gfx_font.py" "$ttf/FreeSerif${face}.ttf" "$pt" "$name" \
      > "$out/$name.h"
  done
done

# The settings sheet and the page strip.
for spec in "FreeSans 9" "FreeSans 8" "FreeSerif 8"; do
  set -- $spec
  name="$1$2pt7b"
  echo "  $name"
  python3 "$root/tools/make_gfx_font.py" "$ttf/$1.ttf" "$2" "$name" > "$out/$name.h"
done

echo "done; headers are in $out"
