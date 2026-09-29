# E-reader simulator

Runs the e-reader on a Mac in a window, so you can work on it without the
e-ink display plugged in. It builds every `.cpp` in `reader/` except the
hardware drivers (`epd.cpp`, `touch.cpp`, `store.cpp`), which `main.cpp`
stands in for -- so it's the same reader code as the ESP32, screens and all.

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
cd ~/kindlev2/simulator
make run
```

| Input | Does |
|---|---|
| Click | Tap: in a book, left third = previous page, middle = controls, right third = next page. In the library, a cover or row opens that book. |
| Right arrow, `n`, space | Next page (or next library page) |
| Left arrow, `p` | Previous page (or previous library page) |
| `v` | **Library only.** Toggle the library view (grid / list) |
| `l` | **Inside a book only.** Back to the library (opens the control bar, then taps its back arrow) |
| `o` | **Library only.** Open book 0 (taps its cover in grid view, its row in list view) -- a fixed-point convenience for headless verification, not a general book picker |
| `q` or Esc | Quit |

Each key is a tap at fixed coordinates, and the same coordinates mean something
else on the other screen: `(400, 20)` toggles the view in the library but turns
the page inside a book. So `v`, `l` and `o` do nothing at all -- and print why
-- when pressed on the screen they do not belong to. `n` and `p` work on both.

`make run` rebuilds automatically when the reader code has changed. What the
device would print to the serial monitor appears in the Terminal.

`make test` runs the host tests for the layout and hit-testing arithmetic --
no window, no hardware. Run it after changing anything in `library.cpp`,
`reading.cpp` or `cover.cpp`.

Reading positions are saved to `reader-sim.state` beside the binary, so the
simulator remembers where you were between runs, as the device does. Delete
it to start fresh.

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

The exception is a file that talks to hardware, such as an SD card driver.
Add it to `HARDWARE_ONLY` in the `Makefile`, and add stand-in versions of its
functions to `main.cpp` (as `main.cpp` does for `epdShowFull`), for example
reading books from a folder on the Mac instead of the SD card.

## Files

| File | What it does |
|---|---|
| `main.cpp` | Opens the window, turns clicks and keys into taps, and stands in for the screen driver (`epdShowFull`, `epdShowPartial`). |
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
