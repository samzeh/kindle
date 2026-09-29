# E-reader

An ESP32 e-reader for the Good Display GDEQ0426T82 4.26" e-paper panel
(800 × 480) with an FT6336 touch panel. It shows a book as justified pages;
tap the right two-thirds of the screen for the next page, the left third to
go back.

## Files

| File | What it does |
|---|---|
| `reader.ino` | Main program. Starts the screen and touch, then passes taps (and `n` / `p` typed in the serial monitor) to `app.cpp`. |
| `app.h` / `app.cpp` | The reader logic: which page is showing, what a tap does, when to use a full refresh. No hardware code, so the simulator runs it too. |
| `board_config.h` | Pin numbers and screen settings (rotation, mirroring, tap direction). The file to edit for a different board. |
| `epd.h` / `epd.cpp` | Screen driver, based on Good Display's demo code. `epdShowFull()` flashes and clears ghosting (~3.7 s); `epdShowPartial()` updates without flashing (~0.8 s). |
| `layout.h` / `layout.cpp` | Typesetting: word wrap, justification, paragraph indents, italics, chapter headings and the % footer. Draws into a picture in memory. |
| `touch.h` / `touch.cpp` | Touch driver. Reports one tap per finger press. |
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

Only `app.cpp`, `layout.cpp` and the files they include run in the
simulator. Changes to `epd.cpp` or `touch.cpp` still need the real hardware.

## Settings

| Setting | Where |
|---|---|
| Pins, rotation, mirroring, tap direction | `board_config.h` |
| Full (flashing) refresh every N page turns (0 = never) | `FULL_REFRESH_EVERY` in `app.cpp` |
| Font and size | the `Fonts/...` includes and `fonts` in `app.cpp` |
| Margins | top of `layout.cpp` |

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
