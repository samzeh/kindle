# E-reader

An ESP32 e-reader for the Good Display GDEQ0426T82 4.26" e-paper panel
(800 × 480) with an FT6336 touch panel. It reads books from `.epub` files: it
opens on a library of books shown with their covers, in either a cover grid
or a list with reading progress. Tap a book to read it; in a book, tap the
left third for the previous page and the right third for the next. While
reading, the screen shows only the text; tap the middle for the controls: a
bar with a back chevron, the current chapter's name and a settings gear (no
settings screen yet), and the page number, counted across the whole book
("Page 42 of 856").

On the device, books are `.epub` files in a `books` folder on a microSD
card (FAT32). In the simulator they are `.epub` files in `simulator/books/`.

## Adding books (device)

1. Wire the microSD module to the ESP32 (unplug USB first):

   | SD module pin | ESP32 pin |
   |---|---|
   | 3.3V (or VCC) | 3V3 |
   | GND | GND |
   | SCK | GPIO 14 |
   | MOSI | GPIO 13 |
   | MISO | GPIO 27 |
   | CS | GPIO 25 |

2. On your computer, format the card **FAT32** (64 GB and larger cards come
   as exFAT; reformat them), make a folder called `books` at the top, and
   copy `.epub` files into it.
3. Put the card in and power the reader on. New books are imported at
   startup ("Adding book 1 of 2"), and each book is prepared the first time
   it is opened. The reader's cache goes in a hidden `.reader` folder on the
   card; deleting it just makes the reader redo that work.

The serial monitor (115200 baud) says `storage: SD card, N MB` if the card is
found, or explains what to check if not.

## How a book is read

```
book.epub --(zip)--> package file: title, author, cover, chapter files, contents
          --(xml + convert)--> text.txt (plain text) + toc.bin (chapters)
          --(layout, once)--> pages.bin (where every page starts)
reading:  text.txt, a few KB at a time --> layout --> screen
```

Pages follow Kindle's conventions: a book's first page is its cover (the
whole cover, as large as fits), and a new page starts at each of the book's
files (title page, contents, chapters), wherever the book's CSS asks for one
(`page-break-before` / `break-before`), and at each top-level chapter in the
table of contents.

A new book's title, author and cover are read when it is first seen, and its
text is converted and its pages counted the first time it is opened (a
"Preparing book..." screen shows while that happens). Everything is kept in a
cache folder per book, so after that it opens straight away. Nothing ever
holds a whole book in memory. See `catalog.h` for the cache layout.

## Files

| File | What it does |
|---|---|
| `reader.ino` | Main program. Starts the screen and touch, then passes taps (and `n` / `p` typed in the serial monitor) to `app.cpp`. |
| `app.h` / `app.cpp` | The screen router: owns the frame buffer, finds the books at startup, normalises taps, hands them to whichever screen is showing, and sends screens to the display (`appRefresh`). |
| `screens.h` | The list of screens, plus `appGoTo()`, `appRefresh()` and the shared canvas. Adding a screen starts here. |
| `library.h` / `library.cpp` | The library (home) screen: the cover grid, the list view with progress, and the toggle between them. |
| `reading.h` / `reading.cpp` | The reading screen: opens and prepares books, counts pages, the controls (back, chapter name, settings, "Page X of Y"), and what a tap does. |
| `busy.h` / `busy.cpp` | The "Preparing book..." / "Adding book" progress screen. |
| `catalog.h` / `catalog.cpp` | The books: finds `.epub` files in `/books`, imports each one's title, author and cover, converts its text when first opened, and manages the cache. |
| `epub.h` / `epub.cpp` | Reads an EPUB's structure: title, author, cover, reading order and table of contents (EPUB 2 and 3). |
| `convert.h` / `convert.cpp` | Turns the chapters' XHTML into the reader's text format, and works out where each chapter starts. |
| `textformat.h` | The reader's text format: one paragraph per line, with markers for headings, unindented paragraphs and italics. |
| `zip.h` / `zip.cpp` | Reads files out of the EPUB (a ZIP file), decompressing as it goes. |
| `xml.h` / `xml.cpp` | A small streaming XML parser. |
| `ascii.h` / `ascii.cpp` | Turns curly quotes, dashes and accented letters into plain ASCII, since the fonts have no others. |
| `textsource.h` / `textsource.cpp` | Where the layout reads text from: memory, or a file through two 4 KB windows. |
| `layout.h` / `layout.cpp` | Typesetting: word wrap, justification, paragraph indents, italics and headings. Draws into a picture in memory. |
| `cover.h` / `cover.cpp` | Covers: decodes a book's JPEG cover once, crops it to fit and dithers it to black and white at two sizes, then draws it from the cache. A title-and-author frame for books without one. |
| `storage.h` | The file interface the EPUB code uses (`/books`, `/.reader`). |
| `storage_sd.cpp` | The device's storage: a microSD card (pins in `board_config.h`). |
| `store.h` / `store.cpp` | Saved reading positions (per book) and library view, in NVS flash. Hardware-only; the simulator stands in for it. |
| `hash.h` | FNV-1a hashing, for book ids and looking up files in an EPUB. |
| `bytes.h` | A stream of bytes (from memory, a file or a ZIP entry), shared by the ZIP, XML and JPEG code. |
| `board_config.h` | Pin numbers and screen settings (rotation, mirroring, tap direction). The file to edit for a different board. |
| `epd.h` / `epd.cpp` | Screen driver, based on Good Display's demo code. `epdShowFull()` flashes and clears ghosting (~3.7 s); `epdShowPartial()` updates without flashing (~0.8 s). |
| `touch.h` / `touch.cpp` | Touch driver. Reports one tap per finger press. |
| `fonts/` | Font sizes Adafruit GFX does not ship (FreeSerif 8pt, for the page number), made with `tools/make_gfx_font.py`. |
| `miniz.c` / `miniz.h` | miniz (MIT), for decompressing EPUB contents; only its decompressor is switched on. |
| `tjpgd.c` / `tjpgd.h` / `tjpgdcnf.h` | TJpgDec, a small JPEG decoder by ChaN (via Bodmer's TJpg_Decoder), configured for grayscale output. |

## Setup (macOS, Arduino IDE)

1. Apple Silicon Macs only, once: install Rosetta, which the Arduino build
   needs. In Terminal:
   `softwareupdate --install-rosetta --agree-to-license`
2. In the Arduino IDE, install:
   - **Boards Manager:** `esp32` by Espressif
   - **Library Manager:** `Adafruit GFX Library` (accept its dependencies)
3. Open `reader/reader.ino`.
4. **Tools → Board → esp32 → ESP32 Dev Module**, and **Tools → Port →**
   the board's USB port (e.g. `/dev/cu.usbserial-0001`). The default
   partition scheme is fine (the program is about 390 KB).
5. Click **Upload**. If it stalls at `Connecting...`, hold the board's BOOT
   button until the upload starts.
6. Optional: **Tools → Serial Monitor** at **115200** baud shows taps,
   refresh times, and timings for importing and preparing books.

## Simulator (no hardware needed)

`../simulator` runs the same reader code on a Mac in a window, reading
`.epub` files from `simulator/books/`. Click to tap; arrow keys or `n` / `p`
turn pages.

```
cd ~/kindlev2
sh tools/fetch_books.sh     # once: four public-domain books
cd simulator
make run
```

See `simulator/README.md` for setup (SDL2 via Homebrew) and details.

Every `.cpp` file in `reader/` runs in the simulator except the hardware
drivers -- `epd.cpp`, `touch.cpp`, `store.cpp` and `storage_sd.cpp` --
which the simulator stands in for. Changes to those still need the real
hardware to test.

## Settings

| Setting | Where |
|---|---|
| Pins, rotation, mirroring, tap direction | `board_config.h` |
| Full (flashing) refresh every N page turns (0 = never) | `FULL_REFRESH_EVERY` in `reading.cpp` |
| Font and size | the `Fonts/...` includes and `fonts` in `reading.cpp` |
| Margins | top of `layout.cpp` |

After changing anything that moves page breaks (fonts, margins, layout
rules), bump `LAYOUT_VERSION` in `reading.h` so every book's pages are
counted again. After changing `convert.cpp`'s output, bump
`CONVERT_VERSION` in `catalog.cpp`.

## Limits

- Fonts are ASCII only, so accented letters lose their accents and other
  scripts (Greek, Hebrew, ...) show as `?`.
- Only baseline JPEG covers are shown; PNG or progressive-JPEG covers get
  the title-and-author frame.
- Images inside books are left out (except drop caps, whose letter is kept).
- DRM-protected EPUBs cannot be read.

## Hardware notes

These were found by testing this panel; keep them when changing the code.

- Leave the screen's CS and clock pins low until the first command
  (`epdBegin()`). Setting them high at startup made the panel ignore every
  command.
- Each no-flash update starts with a reset of the screen controller, as in
  the vendor demo; without it the screen does not update.
- The touch chip's INT pin only pulses briefly on this board, so the touch
  driver polls the chip over I2C instead of watching INT.
- Unplug USB before connecting or reseating the ribbon cables.
- The SD card has its own pins rather than sharing the screen's (Arduino's
  `pinMode` takes pins away from the SPI hardware, and the screen driver
  writes its pins directly). Avoid GPIO19 (front light) and GPIO12 (a boot
  strapping pin).
