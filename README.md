# E-reader simulator

Runs the e-reader on a Mac in a window, so you can work on it without the
e-ink display plugged in. It uses the same reader code as the ESP32
(`reader/app.cpp` and `reader/layout.cpp`).

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
| Click | Tap: left third = previous page, rest = next page |
| Right arrow, `n`, space | Next page |
| Left arrow, `p` | Previous page |
| `q` or Esc | Quit |

`make run` rebuilds automatically when the reader code has changed. What the
device would print to the serial monitor appears in the Terminal.

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
