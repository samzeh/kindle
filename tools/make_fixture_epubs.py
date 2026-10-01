#!/usr/bin/env python3
"""Builds the small test EPUBs in simulator/fixtures/library/.

    python3 tools/make_fixture_epubs.py

The output is deterministic (fixed timestamps), and committed, so the tests
do not need this script to run; re-run it after changing it. Needs Pillow
(python3 -m pip install pillow) for the cover images.

Each book exercises different parts of the EPUB code:
  aardvark.epub  EPUB 3: nav table of contents, stored JPEG cover, ~20 KB of
                 text across 3 chapter files. Nested italics, entities,
                 curly quotes, <br/> in a heading, a page-number span, a
                 Gutenberg licence header, a drop cap, an unclosed <i>, and a
                 table-of-contents target in the middle of a file.
  bramble.epub   EPUB 2: NCX table of contents, cover named by
                 <meta name="cover">, deflated cover.
  coverless.epub No cover and no table of contents (chapters come from
                 headings).
  deep.epub      Package file in a subfolder, hrefs with "../" and %20, a
                 linear="no" spine item, and ZIP data descriptors.
Plus ._aardvark.epub (a macOS resource-fork file) and notes.txt, which the
library must ignore.
"""
import io
import pathlib
import zipfile

from PIL import Image

OUT = pathlib.Path(__file__).resolve().parent.parent / "simulator" / "fixtures" / "library"
STAMP = (2020, 1, 1, 0, 0, 0)

CONTAINER = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="{opf}" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>"""


def jpeg(w, h, split):
    """A grayscale JPEG: black above `split` (0-1 of the height), white below."""
    img = Image.new("L", (w, h), 255)
    img.paste(0, (0, 0, w, int(h * split)))
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=90)
    return buf.getvalue()


def xhtml(title, body):
    return f"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>{title}</title><style>p {{ margin: 0 }}</style></head>
<body>
{body}
</body>
</html>"""


class Unseekable(io.RawIOBase):
    """Makes zipfile write data descriptors, as some EPUB tools do."""

    def __init__(self):
        self.buf = io.BytesIO()

    def writable(self):
        return True

    def write(self, b):
        return self.buf.write(b)


def write_epub(path, files, descriptors=False):
    """files: list of (name, bytes-or-str, compress)."""
    target = Unseekable() if descriptors else io.BytesIO()
    with zipfile.ZipFile(target, "w") as z:
        for name, data, compress in files:
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
            z.writestr(info, data.encode("utf-8") if isinstance(data, str) else data)
    data = target.buf.getvalue() if descriptors else target.getvalue()
    path.write_bytes(data)


def filler(chapter, count):
    """Paragraphs of plain prose, numbered so tests can find them."""
    return "\n".join(
        f"<p>Paragraph {chapter}.{i}: the aardvark walked on through the long grass, "
        f"thinking about ants, the weather, and the curious habits of neighbours who "
        f"never seemed to sleep at the proper time of day.</p>"
        for i in range(1, count + 1)
    )


def aardvark():
    ch1 = xhtml("One", f"""
<section id="pg-header"><p>The Project Gutenberg eBook of Aardvark Stories. LICENCE TEXT.</p></section>
<h2 id="ch1">Chapter 1.<br/>The Burrow</h2>
<p><span class="letra"><img alt="W" src="w.png"/></span>HEN the sun rose, it was &ldquo;early&rdquo; &amp; bright &mdash; very bright.</p>
<p>She said <i>it was <em>truly</em> odd</i>, and café life went on.<span class="pagenum">[12]</span></p>
{filler(1, 25)}
<p id="mid">A new scene begins here, halfway through the first file.</p>
{filler(2, 25)}
""")
    ch2 = xhtml("Two", f"""
<h2 id="ch2">Chapter 2. The Ants</h2>
<p><i>This italic paragraph is never closed.
<p>So this one must not be italic.</p>
{filler(3, 25)}
""")
    ch3 = xhtml("Three", f"""
<h2>Chapter 3. Home</h2>
{filler(4, 25)}
<hr/>
<p>The end.</p>
""")
    nav = xhtml("Contents", """
<nav epub:type="toc"><ol>
  <li><a href="text/one.xhtml#ch1">Chapter 1: The Burrow</a>
    <ol><li><a href="text/one.xhtml#mid">A New Scene</a></li></ol></li>
  <li><a href="text/two.xhtml">Chapter 2: The Ants</a></li>
  <li><a href="text/three.xhtml">Chapter 3: Home</a></li>
</ol></nav>
<nav epub:type="landmarks"><ol><li><a href="text/one.xhtml">Start</a></li></ol></nav>""")
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Aardvark Stories</dc:title>
    <dc:creator>Ann Author</dc:creator>
    <dc:identifier id="id">urn:test:aardvark</dc:identifier>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c1" href="text/one.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="text/two.xhtml" media-type="application/xhtml+xml"/>
    <item id="c3" href="text/three.xhtml" media-type="application/xhtml+xml"/>
    <item id="img" href="images/front.jpg" media-type="image/jpeg" properties="cover-image"/>
  </manifest>
  <spine><itemref idref="c1"/><itemref idref="c2"/><itemref idref="c3"/></spine>
</package>"""
    write_epub(OUT / "aardvark.epub", [
        ("mimetype", "application/epub+zip", False),
        ("META-INF/container.xml", CONTAINER.format(opf="OEBPS/content.opf"), True),
        ("OEBPS/content.opf", opf, True),
        ("OEBPS/nav.xhtml", nav, True),
        ("OEBPS/text/one.xhtml", ch1, True),
        ("OEBPS/text/two.xhtml", ch2, True),
        ("OEBPS/text/three.xhtml", ch3, True),
        ("OEBPS/images/front.jpg", jpeg(120, 180, 0.5), False),
    ])


def bramble():
    ch = xhtml("Bramble", """
<h1>Bramble Tales</h1>
<h2 id="b1">First Tale</h2><p>Thorns and berries.</p>
<h2 id="b2">Second Tale</h2><p>More berries.</p>""")
    ncx = """<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <navMap>
    <navPoint id="n1" playOrder="1"><navLabel><text>First Tale</text></navLabel>
      <content src="tales.html#b1"/></navPoint>
    <navPoint id="n2" playOrder="2"><navLabel><text>Second Tale</text></navLabel>
      <content src="tales.html#b2"/></navPoint>
  </navMap>
</ncx>"""
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:title>Bramble Tales</dc:title>
    <dc:creator opf:role="aut">Bea Brambleton</dc:creator>
    <meta name="cover" content="coverimg"/>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="coverimg" href="art.jpeg" media-type="image/jpeg"/>
    <item id="tales" href="tales.html" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx"><itemref idref="tales"/></spine>
</package>"""
    write_epub(OUT / "bramble.epub", [
        ("mimetype", "application/epub+zip", False),
        ("META-INF/container.xml", CONTAINER.format(opf="content.opf"), True),
        ("content.opf", opf, True),
        ("toc.ncx", ncx, True),
        ("tales.html", ch, True),
        ("art.jpeg", jpeg(90, 135, 0.25), True),
    ])


def coverless():
    ch = xhtml("Notes", """
<h1>Coverless Notes</h1>
<h2>Part One</h2><p>Plain notes without a cover.</p>
<h2>Part Two</h2><p>Still no cover.</p>""")
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Coverless Notes</dc:title>
    <dc:creator>Cy Nocover</dc:creator>
  </metadata>
  <manifest><item id="n" href="notes.xhtml" media-type="application/xhtml+xml"/></manifest>
  <spine><itemref idref="n"/></spine>
</package>"""
    write_epub(OUT / "coverless.epub", [
        ("mimetype", "application/epub+zip", False),
        ("META-INF/container.xml", CONTAINER.format(opf="package.opf"), True),
        ("package.opf", opf, True),
        ("notes.xhtml", ch, True),
    ])


def deep():
    ch = xhtml("Deep", """<h2 id="d1">Down Deep</h2><p>A book in a deep folder.</p>""")
    extra = xhtml("Extra", "<p>Not in the reading order.</p>")
    nav = xhtml("Contents", """<nav epub:type="toc"><ol>
<li><a href="../text/chapter%20one.xhtml#d1">Down Deep</a></li></ol></nav>""")
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>Deep Folder</dc:title>
    <dc:creator>Dee Pfolder</dc:creator>
  </metadata>
  <manifest>
    <item id="nav" href="../nav/nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="c" href="../text/chapter%20one.xhtml" media-type="application/xhtml+xml"/>
    <item id="x" href="../text/extra.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine><itemref idref="c"/><itemref idref="x" linear="no"/></spine>
</package>"""
    write_epub(OUT / "deep.epub", [
        ("mimetype", "application/epub+zip", False),
        ("META-INF/container.xml", CONTAINER.format(opf="OPS/package/book.opf"), True),
        ("OPS/package/book.opf", opf, True),
        ("OPS/nav/nav.xhtml", nav, True),
        ("OPS/text/chapter one.xhtml", ch, True),
        ("OPS/text/extra.xhtml", extra, True),
    ], descriptors=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    aardvark()
    bramble()
    coverless()
    deep()
    (OUT / "._aardvark.epub").write_bytes(b"\x00\x05\x16\x07 macOS resource fork")
    (OUT / "notes.txt").write_text("Not a book.\n")
    print(f"wrote fixtures to {OUT}")


if __name__ == "__main__":
    main()
