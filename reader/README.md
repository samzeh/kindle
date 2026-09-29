# E-reader

An ESP32 e-reader for the Good Display GDEQ0426T82 4.26" e-paper panel
(800 × 480) with an FT6336 touch panel. It opens on a library of books shown
with their covers, in either a cover grid or a list with reading progress.
Tap a book to read it; in a book, tap the left third for the previous page,
the right third for the next, and the middle for a bar with a way back to
the library.

## Files

| File | What it does |
|---|---|
| `reader.ino` | Main program. Starts the screen and touch, then passes taps (and `n` / `p` typed in the serial monitor) to `app.cpp`. |
| `app.h` / `app.cpp` | The screen router: owns the frame buffer, normalises taps, and hands them to whichever screen is showing. |
| `screens.h` | The list of screens, plus `appGoTo()` and the shared canvas. Adding a screen starts here. |
| `reading.h` / `reading.cpp` | The reading screen: which page of which book is showing, the control bar, and what a tap does. |
| `library.h` / `library.cpp` | The library (home) screen: the cover grid, the list view with progress, and the toggle between them. |
| `cover.h` / `cover.cpp` | Draws a cover at any size, and sets the title and author in a frame when a book has no cover art. |
| `store.h` / `store.cpp` | Saved reading positions and library view, in NVS flash. Hardware-only; the simulator stands in for it. |
| `board_config.h` | Pin numbers and screen settings (rotation, mirroring, tap direction). The file to edit for a different board. |
| `epd.h` / `epd.cpp` | Screen driver, based on Good Display's demo code. `epdShowFull()` flashes and clears ghosting (~3.7 s); `epdShowPartial()` updates without flashing (~0.8 s). |
| `layout.h` / `layout.cpp` | Typesetting: word wrap, justification, paragraph indents, italics, chapter headings and the % footer. Draws into a picture in memory. |
| `touch.h` / `touch.cpp` | Touch driver. Reports one tap per finger press. |
| `books.h` / `books.cpp` | The shelf: title, author, text and cover for each book. |
| `texts/` | The book texts, in the format `layout.h` documents. |
| `covers/` | Generated 1-bit cover arrays; see `tools/make_cover.py`. None are committed yet, so every book on the shelf currently shows the typographic placeholder. |
| `sample_text.h` | Placeholder book: *Pride and Prejudice*, chapters I–III. One paragraph per line, `# ` marks a heading, `_underscores_` mark italics. |

## Setup (macOS, Arduino IDE)

1. Apple Silicon Macs only, once: install Rosetta, which the Arduino build
   needs. In Terminal:
   `softwareupdate --install-rosetta --agree-to-license`
2. In the Arduino IDE, install:
   - **Boards Manager:** `esp32` by Espressif
   - **Library Manager:** `Adafruit GFX Library` (accept its dependencies)
3. Open `reader/reader.ino`.
4. **Tools → Board → esp32 → ESP32 Dev Module**, and **Tools → Port →**
   the board's USB port (e.g. `/dev/cu.usbserial-0001`).
5. Click **Upload**. If it stalls at `Connecting...`, hold the board's BOOT
   button until the upload starts.
6. Optional: **Tools → Serial Monitor** at **115200** baud shows taps and
   refresh times.

## Simulator (no hardware needed)

`../simulator` runs the same reader code on a Mac in a window, so you can
work on it without the display plugged in. Click to tap; arrow keys or
`n` / `p` turn pages.

```
cd ~/kindlev2/simulator
make run
```

See `simulator/README.md` for setup (SDL2 via Homebrew) and details.

Every `.cpp` file in `reader/` runs in the simulator except the hardware
drivers -- `epd.cpp`, `touch.cpp` and `store.cpp` -- which `main.cpp` stands
in for. Changes to those three still need the real hardware to test.

## Persistence

Reading positions and the chosen library view are saved to NVS flash
(`store.cpp`) and restored on boot. This has been verified across separate
runs in the simulator, which stands in for NVS with a file
(`reader-sim.state`); it has **not** yet been verified on real hardware,
since no ESP32 board was available while this was built.

## Settings

| Setting | Where |
|---|---|
| Pins, rotation, mirroring, tap direction | `board_config.h` |
| Full (flashing) refresh every N page turns (0 = never) | `FULL_REFRESH_EVERY` in `reading.cpp` |
| Font and size | the `Fonts/...` includes and `fonts` in `reading.cpp` |
| Margins | top of `layout.cpp` |
| Which books are on the shelf | `books.cpp` |

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
