#!/usr/bin/env python3
"""Convert the host test's binary PGM output to PNG for visual inspection."""

import struct
import sys
import zlib


def read_pgm(path):
    with open(path, "rb") as f:
        data = f.read()

    fields = []
    pos = 0
    while len(fields) < 4:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while data[pos : pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1

    assert fields[0] == b"P5", f"not a binary PGM: {fields[0]!r}"
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos : pos + w * h]


def write_png(path, w, h, gray):
    raw = b"".join(b"\x00" + gray[y * w : (y + 1) * w] for y in range(h))

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)


def main():
    if len(sys.argv) < 3:
        print("usage: pgm2png.py IN.pgm OUT.png", file=sys.stderr)
        return 1
    w, h, gray = read_pgm(sys.argv[1])
    write_png(sys.argv[2], w, h, gray)
    print(f"{sys.argv[2]}: {w}x{h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
