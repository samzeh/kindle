#!/usr/bin/env python3
"""Build a real EPUB for the host tests.

Deliberately exercises the awkward parts of the format: a nested OEBPS
directory, percent-encoded and ../-relative hrefs, both stored and deflated
entries, an NCX table of contents, and entity-heavy XHTML.
"""

import argparse
import zipfile

CHAPTER_BODY = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{title}</title>
<style>body {{ margin: 1em; }}</style>
</head>
<body>
<h1>{title}</h1>
<p>It is a truth universally acknowledged, that a single man in possession of a
good fortune, must be in want of a wife. However little known the feelings or
views of such a man may be on his first entering a neighbourhood, this truth is
so well fixed in the minds of the surrounding families, that he is considered
as the <em>rightful property</em> of some one or other of their
<strong>daughters</strong>.</p>
<p>&ldquo;My dear Mr.&#160;Bennet,&rdquo; said his lady to him one day, &ldquo;have
you heard that Netherfield Park is let at last?&rdquo; Mr. Bennet replied that he
had not&#8212;and cared little either way.</p>
<blockquote>Whenever I find myself growing grim about the mouth; whenever it is a
damp, drizzly November in my soul.</blockquote>
<ul>
<li>Caf&eacute;, na&#239;ve, r&#xE9;sum&#xE9;</li>
<li>A rather longer list item that is certain to wrap across more than one line
on a narrow column</li>
</ul>
<p>A line<br/>broken in two.</p>
<hr/>
<p>{filler}</p>
</body>
</html>
"""

FILLER = (
    "The family of Dashwood had long been settled in Sussex. Their estate was "
    "large, and their residence was at Norland Park, in the centre of their "
    "property, where, for many generations, they had lived in so respectable a "
    "manner as to engage the general good opinion of their surrounding "
    "acquaintance. "
)


def build(path, chapters):
    titles = [f"Chapter {i + 1}" for i in range(chapters)]

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        # The mimetype entry must be first and stored uncompressed, which also
        # gives the reader a stored entry to handle.
        z.writestr(
            zipfile.ZipInfo("mimetype"),
            "application/epub+zip",
            compress_type=zipfile.ZIP_STORED,
        )

        z.writestr(
            "META-INF/container.xml",
            '<?xml version="1.0"?>\n'
            '<container version="1.0" '
            'xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
            "<rootfiles>"
            '<rootfile full-path="OEBPS/content.opf" '
            'media-type="application/oebps-package+xml"/>'
            "</rootfiles></container>",
        )

        manifest, spine, navpoints = [], [], []
        for i, title in enumerate(titles):
            name = f"text/chap %02d.xhtml" % i  # a space, to force %20 encoding
            # Large enough that inflate has to wrap the 32KB sliding window.
            z.writestr(
                f"OEBPS/{name}",
                CHAPTER_BODY.format(title=title, filler=FILLER * 110),
            )
            href = name.replace(" ", "%20")
            manifest.append(
                f'<item id="c{i}" href="{href}" media-type="application/xhtml+xml"/>'
            )
            spine.append(f'<itemref idref="c{i}"/>')
            navpoints.append(
                f'<navPoint id="n{i}" playOrder="{i + 1}">'
                f"<navLabel><text>{title}</text></navLabel>"
                f'<content src="{href}"/></navPoint>'
            )

        manifest.append(
            '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
        )

        z.writestr(
            "OEBPS/content.opf",
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
            'unique-identifier="bookid">'
            '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
            "<dc:title>Pride and Prejudice &amp; Other Tests</dc:title>"
            "<dc:creator>Jane Austen</dc:creator>"
            '<dc:identifier id="bookid">urn:uuid:test-0001</dc:identifier>'
            "</metadata>"
            f'<manifest>{"".join(manifest)}</manifest>'
            f'<spine toc="ncx">{"".join(spine)}</spine>'
            "</package>",
        )

        z.writestr(
            "OEBPS/toc.ncx",
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
            "<docTitle><text>Pride and Prejudice</text></docTitle>"
            f'<navMap>{"".join(navpoints)}</navMap></ncx>',
        )

    print(f"wrote {path} with {chapters} chapters")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("--chapters", type=int, default=5)
    args = ap.parse_args()
    build(args.output, args.chapters)
