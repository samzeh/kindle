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
