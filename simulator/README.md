# E-reader simulator

Runs the e-reader on a Mac in a window, so you can work on it without the
e-ink display plugged in. It builds every `.cpp` in `reader/` except the
hardware drivers (`epd.cpp`, `touch.cpp`, `store.cpp`, `storage_sd.cpp`),
which `main.cpp` and `host_platform.cpp` stand in for -- so it's the same
reader code as the ESP32, screens and all.

Books are the `.epub` files in `simulator/books/`, which stands in for the
device's book storage.

## Setup (once)

1. **Xcode command line tools** (the C++ compiler and `make`). In Terminal:
   `xcode-select --install`
   If it says they are already installed, skip this step.
2. **Homebrew** (installs developer tools): see https://brew.sh
3. **SDL2** (opens the window): `brew install sdl2`
4. **Adafruit GFX library**: install it in the Arduino IDE's Library
   Manager, as for the device. The simulator reads it from
   `~/Documents/Arduino/libraries/Adafruit_GFX_Library`. If yours is
   somewhere else, run `make run GFX=/path/to/Adafruit_GFX_Library`.

## Running

```
cd ~/kindlev2
sh tools/fetch_books.sh     # once: downloads four books into simulator/books/
cd simulator
make run
```

Put any other `.epub` files in `simulator/books/` too. The first time a book
is seen its title, author and cover are read, and the first time it is
opened its text is converted and its pages counted; all of that is kept in
`simulator/.cache/`, as the device keeps it on its storage.

| Option | Does |
|---|---|
| `--books DIR` | Read books from DIR instead of `books/` |
| `--cache DIR` | Keep the cache in DIR instead of `.cache/` |
| `--reset-cache` | Delete the cache first, as if every book were new (also forgets reading positions) |

| Input | Does |
|---|---|
| Click | Tap: in a book, left third = previous page, middle = controls, right third = next page. In the library, a cover or row opens that book. |
| Right arrow, `n`, space | Next page (or next library page) |
| Left arrow, `p` | Previous page (or previous library page) |
| `v` | **Library only.** Toggle the library view (grid / list) |
| `l` | **Inside a book only.** Back to the library (opens the controls, then taps the back chevron) |
| `o` | **Library only.** Open book 0 (taps its cover in grid view, its row in list view) -- a fixed-point convenience for headless verification, not a general book picker |
| `c` | **Inside a book only.** Show the controls (chapter name, page number) |
| `q` or Esc | Quit |

Each key is a tap at fixed coordinates, and the same coordinates mean something
else on the other screen: `(400, 20)` toggles the view in the library but turns
the page inside a book. So `v`, `l`, `o` and `c` do nothing at all -- and print why
-- when pressed on the screen they do not belong to. `n` and `p` work on both.

`make run` rebuilds automatically when the reader code has changed. What the
device would print to the serial monitor appears in the Terminal.

`make test` runs the host tests -- no window, no hardware. They use the small
books in `fixtures/library/` (made by `tools/make_fixture_epubs.py`) and cover
the EPUB code, layout, pagination, covers, the library and the reading
screen. Run it after changing anything in `reader/`.

`make epub-dump && ./epub-dump book.epub [--text]` prints what the reader
makes of an EPUB: title, author, cover, chapters and where each lands, and
(with `--text`) the converted text.

Reading positions and the library view are saved in `.cache/settings.txt`,
so the simulator remembers where you were between runs, as the device does.

To save the screen as an image without opening a window:
`./reader-sim --screenshot page.bmp nnn` (turns 3 pages first; `p` goes back).

## What it does and does not test

- **Same as the device:** text layout, fonts, margins, page turns, what taps
  do, when full refreshes happen.
- **Not tested:** the real screen and touch drivers (`reader/epd.cpp`,
  `reader/touch.cpp`), hardware timing, and memory limits. Test changes to
  those on the device.

## Adding new files

Any new `.cpp` file in `reader/` (for example `library.cpp` for a library
page) is picked up automatically, by both the Arduino IDE and the simulator.

The exception is a file that talks to hardware, such as the SD card driver
(`storage_sd.cpp`). Add it to `HARDWARE_ONLY` in the `Makefile`, and give the
simulator a stand-in (as `host_platform.cpp` stands in for `storage.h`).

## Files

| File | What it does |
|---|---|
| `main.cpp` | Opens the window, turns clicks and keys into taps, and stands in for the screen driver (`epdShowFull`, `epdShowPartial`). |
| `host_platform.cpp` / `.h` | Stands in for book storage (`reader/storage.h`, a folder of EPUBs plus a cache folder) and saved settings (`reader/store.h`). Shared by the simulator and the tests. |
| `tests.cpp`, `test_epub.cpp`, `check.h` | The host tests (`make test`). |
| `epubdump.cpp` | The `epub-dump` tool. |
| `fixtures/library/` | Small test EPUBs, made by `tools/make_fixture_epubs.py`. |
| `books/`, `.cache/` | Your books and the simulator's cache (not in git). |
| `shim/Arduino.h` | Minimal stand-ins for Arduino features the reader code uses (`Serial.printf`, `millis`), so it compiles on a Mac. |
| `shim/*.h` (others) | Empty files standing in for Arduino-only headers that Adafruit GFX includes. |
| `Makefile` | Build instructions: compiles `main.cpp` with the reader code and Adafruit GFX. |

## Troubleshooting

| Message | Fix |
|---|---|
| `cd: no such file or directory` | Use the full path: `cd ~/kindlev2/simulator` |
| `No rule to make target 'run'` | You are not in the `simulator` folder; `cd` there first. |
| `sdl2-config: command not found` or `SDL.h not found` | `brew install sdl2` |
| `Adafruit_GFX.h not found` | Install Adafruit GFX (setup step 4), or pass `GFX=...` |
| Library says "No books yet" | Put `.epub` files in `simulator/books/` (`sh ../tools/fetch_books.sh`) |
