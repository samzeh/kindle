"""Checks that make_cover.py emits a header matching reader/cover.h's layout."""
import pathlib
import re
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).parent
COVER_W, COVER_H, ROW_BYTES = 204, 306, 26


def test_emits_correctly_sized_array():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        src = tmp / "in.png"
        # Left half black, right half white, so bit order is observable.
        img = Image.new("L", (100, 150), 255)
        for y in range(150):
            for x in range(50):
                img.putpixel((x, y), 0)
        img.save(src)

        subprocess.run(
            [sys.executable, str(HERE / "make_cover.py"), str(src), "testbook",
             "--out-dir", str(tmp)],
            check=True,
        )

        text = (tmp / "testbook.h").read_text()
        assert "COVER_TESTBOOK" in text
        values = re.findall(r"0x[0-9a-fA-F]{2}", text)
        assert len(values) == ROW_BYTES * COVER_H

        # First byte of a row is the left edge, which is black -> all ink bits.
        assert int(values[0], 16) == 0xFF
        # Last byte of a row is the right edge, which is white -> no ink bits.
        assert int(values[ROW_BYTES - 1], 16) == 0x00


def test_pins_bit_order_within_a_byte():
    # test_emits_correctly_sized_array only checks homogeneous bytes (0xFF,
    # 0x00), which are identical under MSB-first (0x80 >> x%8) and
    # LSB-first (0x01 << x%8) packing -- they can't catch a flipped bit
    # order. This test uses a single asymmetric pixel to discriminate the
    # two conventions: if this ever fails, the packing convention has
    # flipped and every generated cover will render mirrored/inverted.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        src = tmp / "in.png"
        # Already at COVER_W x COVER_H so the resize is a no-op, and pure
        # 0/255 input means Floyd-Steinberg diffuses zero error (nothing to
        # spread when every pixel is already at an extreme), so the
        # conversion is exact and deterministic.
        img = Image.new("L", (COVER_W, COVER_H), 255)
        img.putpixel((1, 0), 0)  # single black pixel, second column
        img.save(src)

        subprocess.run(
            [sys.executable, str(HERE / "make_cover.py"), str(src), "testbook",
             "--out-dir", str(tmp)],
            check=True,
        )

        text = (tmp / "testbook.h").read_text()
        values = re.findall(r"0x[0-9a-fA-F]{2}", text)

        # x=1 is the only ink bit in row 0's first byte. MSB-first, that's
        # bit position 1 from the left: 0x80 >> 1 == 0x40. Under LSB-first
        # packing this byte would instead be 0x02.
        assert int(values[0], 16) == 0x40
