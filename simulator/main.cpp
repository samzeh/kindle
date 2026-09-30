// Desktop simulator for the e-reader. Builds every .cpp in reader/ except
// the hardware drivers (epd.cpp, touch.cpp, store.cpp), which this file
// stands in for -- so it runs the same reader code as the ESP32, screens
// and all, and shows it in a window.
//
//   click          tap: in a book, left third = previous page, middle =
//                  controls, right third = next page. In the library, a
//                  cover or row opens that book.
//   right / left   next / previous page (also n / p; pages the library too)
//   v              toggle the library view (grid / list) -- library only
//   l              back to the library -- inside a book only
//   o              open book 0 (taps its cover or row) -- library only
//   Esc / q        quit
//
// The three screen-specific keys do nothing, and say so, when pressed on the
// other screen: each is a tap at fixed coordinates, and those coordinates mean
// something else over there. See simKey.
//
// ./reader-sim --screenshot page.bmp nnp  turns pages as listed (n = next,
// p = previous, v = toggle the library view, l = back to the library, o =
// open book 0), saves the screen to page.bmp and exits.
#include <SDL.h>
#include <vector>

#include "Arduino.h"
#include "app.h"
#include "board_config.h"
#include "epd.h"
#include "screens.h"
#include "store.h"

SerialPort Serial;

static const bool PORTRAIT = SCREEN_ROTATION % 2 == 1;
static const int SCREEN_W = PORTRAIT ? EPD_NATIVE_HEIGHT : EPD_NATIVE_WIDTH;
static const int SCREEN_H = PORTRAIT ? EPD_NATIVE_WIDTH : EPD_NATIVE_HEIGHT;

// Colors roughly like e-paper under a light.
static const uint32_t PAPER = 0xFFEAEAE4;
static const uint32_t INK = 0xFF202020;

// A full refresh flashes black then white before showing the page.
static const uint32_t FLASH_MS = 150;

static SDL_Renderer *renderer;
static SDL_Texture *texture;
static std::vector<uint32_t> pixels(SCREEN_W *SCREEN_H);

// Finds the pixel of the panel image (native 800 x 480 layout) that appears
// at screen position (x, y): the inverse of GFXcanvas1's rotation.
static void toNative(int x, int y, int &nx, int &ny) {
  switch (SCREEN_ROTATION) {
    case 0: nx = x; ny = y; break;
    case 1: nx = EPD_NATIVE_WIDTH - 1 - y; ny = x; break;
    case 2: nx = EPD_NATIVE_WIDTH - 1 - x; ny = EPD_NATIVE_HEIGHT - 1 - y; break;
    default: nx = y; ny = EPD_NATIVE_HEIGHT - 1 - x; break;
  }
}

static void redraw() {
  SDL_UpdateTexture(texture, nullptr, pixels.data(), SCREEN_W * sizeof(uint32_t));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
}

static void showSolid(uint32_t color, uint32_t ms) {
  std::fill(pixels.begin(), pixels.end(), color);
  redraw();
  SDL_Delay(ms);
}

static void showImage(const uint8_t *image) {
  const int rowBytes = EPD_NATIVE_WIDTH / 8;
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = 0; x < SCREEN_W; x++) {
      int nx, ny;
      toNative(x, y, nx, ny);
      bool white = image[ny * rowBytes + nx / 8] & (0x80 >> (nx % 8));
      pixels[y * SCREEN_W + x] = white ? PAPER : INK;
    }
  }
  redraw();
}

// ---- Stand-ins for the screen driver (reader/epd.h) ----

void epdBegin() {}

void epdShowFull(const uint8_t *image) {
  printf("screen: full refresh (the device flashes here)\n");
  showSolid(INK, FLASH_MS);
  showSolid(PAPER, FLASH_MS);
  showImage(image);
}

void epdShowPartial(const uint8_t *image) {
  showImage(image);
}

void epdSleep() {}

// ---- Stand-in for NVS (reader/store.h), backed by a file ----

static const char *STATE_FILE = "reader-sim.state";
static const int MAX_BOOKS = 32;

struct SimState {
  uint32_t progress[MAX_BOOKS];
  bool saved[MAX_BOOKS];
  uint8_t view;
};

static SimState simState = {};

static void simStateWrite() {
  FILE *f = fopen(STATE_FILE, "wb");
  if (!f) return;
  fwrite(&simState, sizeof(simState), 1, f);
  fclose(f);
}

void storeBegin() {
  FILE *f = fopen(STATE_FILE, "rb");
  if (!f) return;
  if (fread(&simState, sizeof(simState), 1, f) != 1) simState = SimState{};
  fclose(f);
}

void storeSaveProgress(uint8_t book, uint32_t offset, bool italic) {
  if (book >= MAX_BOOKS) return;
  simState.progress[book] = storePack(offset, italic);
  simState.saved[book] = true;
  simStateWrite();
}

bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic) {
  if (book >= MAX_BOOKS || !simState.saved[book]) return false;
  storeUnpack(simState.progress[book], offset, italic);
  return true;
}

void storeSaveView(uint8_t view) {
  simState.view = view;
  simStateWrite();
}

uint8_t storeLoadView() {
  return simState.view;
}

// ---- Window and input ----

// Turns one input character into the app action it stands for: shared by the
// interactive key handler and the --screenshot sequence parser, so a key
// added to one is never missing from the other. Unrecognised characters
// (and 'n') turn the page forward; 'p' turns it back.
//
// Each key stands for a tap at fixed coordinates, and the same coordinates
// mean different things on different screens: (400, 20) toggles the library
// view but turns the page inside a book, and a list row is hit anywhere across
// its width where a grid cell is not. So a key that only makes sense on one
// screen is ignored on the other rather than firing a tap that quietly does
// something else -- with no board to hand, the simulator is the only evidence
// a change to the reader has, and a screenshot whose caption is wrong is worse
// than no screenshot.
static void simKey(char key) {
  const Screen screen = appCurrentScreen();
  switch (key) {
    case 'v':  // library only: inside a book, (400, 20) turns the page
      if (screen != SCREEN_LIBRARY) {
        printf("'v' ignored: it toggles the library view, and a book is open.\n");
        break;
      }
      appTap(400, 20);
      break;
    case 'l':  // reading only: in the library's list view, (240, 400) opens a book
      if (screen != SCREEN_READING) {
        printf("'l' ignored: it leaves a book, and the library is already up.\n");
        break;
      }
      appTap(240, 400);  // middle third: raise the control bar
      appTap(40, 30);    // its back arrow
      break;
    // A fixed point that hits book 0 in either library view, so 'o' means the
    // same thing whichever is up: x=126 is inside the grid's left column
    // (x 24-227) and anywhere along a list row; y=80 is inside the grid's top
    // row (y 54-403) and inside list row 0 (y 50-167), and below the 48px
    // header in both. A convenience for headless verification of a fixed
    // point, not a general "book picker".
    case 'o':
      if (screen != SCREEN_LIBRARY) {
        printf("'o' ignored: it opens a book from the library, and one is already open.\n");
        break;
      }
      appTap(126, 80);
      break;
    default: appTurnPage(key == 'p' ? -1 : 1); break;  // both screens page
  }
}

int main(int argc, char **argv) {
  const char *screenshotFile = nullptr;
  const char *screenshotTurns = "";
  if (argc >= 3 && strcmp(argv[1], "--screenshot") == 0) {
    screenshotFile = argv[2];
    if (argc >= 4) screenshotTurns = argv[3];
  }

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // Shrink the window if the screen is too short for 800 px.
  float scale = 1.0f;
  SDL_Rect usable;
  if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.h - 40 < SCREEN_H) {
    scale = (usable.h - 40) / (float)SCREEN_H;
  }

  SDL_Window *window = SDL_CreateWindow("E-reader simulator", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, (int)(SCREEN_W * scale),
                                        (int)(SCREEN_H * scale),
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                          (screenshotFile ? SDL_WINDOW_HIDDEN : 0));
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);  // clicks arrive in screen pixels
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              SCREEN_W, SCREEN_H);

  appBegin();

  if (screenshotFile) {
    for (const char *c = screenshotTurns; *c; c++) simKey(*c);
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormatFrom(
      pixels.data(), SCREEN_W, SCREEN_H, 32, SCREEN_W * sizeof(uint32_t), SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(shot, screenshotFile);
    SDL_FreeSurface(shot);
    SDL_Quit();
    return 0;
  }

  printf("Click to tap (left third = back), arrow keys or n/p to turn pages, "
         "v to toggle library view, l to return to the library, o to open "
         "book 0, q to quit.\n");

  SDL_Event event;
  while (SDL_WaitEvent(&event)) {
    if (event.type == SDL_QUIT) break;
    if (event.type == SDL_WINDOWEVENT) redraw();
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
      int x = event.button.x, y = event.button.y;
      if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) continue;
      // Convert to raw touch coordinates, as the touch chip would report them.
      if (TOUCH_FLIP_X) x = TOUCH_WIDTH - 1 - x;
      appTap((uint16_t)x, (uint16_t)y);
    }
    if (event.type == SDL_KEYDOWN) {
      switch (event.key.keysym.sym) {
        case SDLK_RIGHT: case SDLK_n: case SDLK_SPACE: simKey('n'); break;
        case SDLK_LEFT: case SDLK_p: simKey('p'); break;
        case SDLK_v: simKey('v'); break;
        case SDLK_l: simKey('l'); break;
        case SDLK_o: simKey('o'); break;
        case SDLK_ESCAPE: case SDLK_q: goto quit;
      }
    }
  }
quit:
  SDL_Quit();
  return 0;
}
