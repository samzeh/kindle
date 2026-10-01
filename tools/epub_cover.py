#!/usr/bin/env python3
"""Extracts an EPUB's cover image, byte for byte, as a C header.

    python3 tools/epub_cover.py book.epub FRANKENSTEIN > reader/covers/frankenstein.h

The header defines FRANKENSTEIN_COVER_JPEG (the image file's bytes) and
FRANKENSTEIN_COVER_JPEG_SIZE. The e-reader decodes it at run time, as it will
once books are read from an SD card; only where the bytes come from changes.

The cover is found the way reading systems do: META-INF/container.xml names
the package (OPF) file, whose manifest marks the cover with
properties="cover-image" (EPUB 3) or a <meta name="cover"> entry (EPUB 2).
Only baseline JPEG covers are supported, since that is what the decoder
handles; anything else is reported as an error.
"""
import posixpath
import sys
import zipfile
from xml.etree import ElementTree


def find_cover(epub):
    container = ElementTree.fromstring(epub.read("META-INF/container.xml"))
    opf_path = container.find(".//{*}rootfile").get("full-path")
    opf = ElementTree.fromstring(epub.read(opf_path))
    items = opf.findall(".//{*}manifest/{*}item")

    cover = next((i for i in items if "cover-image" in (i.get("properties") or "").split()), None)
    if cover is None:
        meta = opf.find('.//{*}metadata/{*}meta[@name="cover"]')
        if meta is not None:
            cover = next((i for i in items if i.get("id") == meta.get("content")), None)
    if cover is None:
        sys.exit("no cover image found in the manifest")

    path = posixpath.normpath(posixpath.join(posixpath.dirname(opf_path), cover.get("href")))
    return path, cover.get("media-type"), epub.read(path)


def jpeg_info(data):
    """Returns (width, height, is_progressive), or None if not a JPEG."""
    if data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 4 <= len(data) and data[i] == 0xFF:
        marker = data[i + 1]
        length = int.from_bytes(data[i + 2:i + 4], "big")
        if marker in (0xC0, 0xC1, 0xC2):
            height = int.from_bytes(data[i + 5:i + 7], "big")
            width = int.from_bytes(data[i + 7:i + 9], "big")
            return width, height, marker == 0xC2
        i += 2 + length
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    epub_path, name = sys.argv[1], sys.argv[2].upper()
    path, media_type, data = find_cover(zipfile.ZipFile(epub_path))

    info = jpeg_info(data)
    if info is None:
        sys.exit(f"cover {path} is {media_type}, not a JPEG")
    width, height, progressive = info
    if progressive:
        sys.exit(f"cover {path} is a progressive JPEG, which the decoder does not support")

    print(f"// Cover of {posixpath.basename(epub_path)}: {path}, extracted unchanged by")
    print(f"// tools/epub_cover.py. {media_type}, {width} x {height}, {len(data)} bytes.")
    print("#pragma once")
    print("#include <stdint.h>")
    print()
    print(f"static const uint32_t {name}_COVER_JPEG_SIZE = {len(data)};")
    print(f"static const uint8_t {name}_COVER_JPEG[] = {{")
    for i in range(0, len(data), 20):
        print("  " + ", ".join(f"0x{b:02X}" for b in data[i:i + 20]) + ",")
    print("};")


if __name__ == "__main__":
    main()
