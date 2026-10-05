// The reader's own text format: what an EPUB is converted into, and what
// the page layout (layout.h) reads.
//
//   - Plain ASCII text, one paragraph per line ('\n' between paragraphs).
//   - TXT_HEADING at the start of a paragraph: a heading (bold, centred).
//   - TXT_NOINDENT at the start of a paragraph: no first-line indent (the
//     paragraph after a heading, the start of a chapter file, after <hr>).
//   - TXT_ITALIC toggles italics. Italics never continue past a '\n'.
//   - TXT_PAGEBREAK at the very start of a paragraph (before any other
//     marker): the paragraph starts a new page (a chapter, the title page,
//     ...). Ignored at the top of a page that is still empty.
//
// The markers are control characters, which the fonts do not draw and which
// take no width, so book text never needs escaping.
#pragma once

static const char TXT_ITALIC = '\x01';
static const char TXT_HEADING = '\x02';
static const char TXT_NOINDENT = '\x03';
static const char TXT_PAGEBREAK = '\x04';
