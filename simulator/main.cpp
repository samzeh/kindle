// Desktop simulator for the e-reader. Builds every .cpp in reader/ except
// the hardware drivers (epd.cpp, touch.cpp, store.cpp, storage_sd.cpp):
// this file stands in for the screen, and host_platform.cpp for storage and
// saved settings -- so it runs the same reader code as the ESP32, screens
// and all, and shows it in a window.
//
// Books are the .epub files in simulator/books/ (tools/fetch_books.sh
// downloads four), and the reader's cache goes in simulator/.cache/:
//   --books DIR     read books from DIR instead
//   --cache DIR     keep the cache in DIR instead
//   --reset-cache   delete the cache first, as if every book were new
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
// open book 0, c = show the control bar), saves the screen to page.bmp and
// exits.
#include <SDL.h>
#include <string>
#include <vector>

#include "Arduino.h"
#include "app.h"
#include "board_config.h"
#include "epd.h"
#include "host_platform.h"
#include "screens.h"

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
      appTap(126, 100);
      break;
    case 'c':  // reading only: a tap in the middle third raises the control bar
      if (screen != SCREEN_READING) {
        printf("'c' ignored: it shows a book's control bar, and no book is open.\n");
        break;
      }
      appTap(240, 400);
      break;
    default: appTurnPage(key == 'p' ? -1 : 1); break;  // both screens page
  }
}

int main(int argc, char **argv) {
  const char *screenshotFile = nullptr;
  const char *screenshotTurns = "";
  const char *booksDir = "books", *cacheDir = ".cache";
  bool resetCache = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshotFile = argv[++i];
      if (i + 1 < argc && argv[i + 1][0] != '-') screenshotTurns = argv[++i];
    } else if (strcmp(argv[i], "--books") == 0 && i + 1 < argc) {
      booksDir = argv[++i];
    } else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) {
      cacheDir = argv[++i];
    } else if (strcmp(argv[i], "--reset-cache") == 0) {
      resetCache = true;
    } else {
      fprintf(stderr, "unknown option %s\n", argv[i]);
      return 2;
    }
  }
  if (resetCache) {
    std::string cmd = std::string("rm -rf '") + cacheDir + "'";
    if (system(cmd.c_str()) != 0) fprintf(stderr, "could not delete %s\n", cacheDir);
  }
  hostStorageSetRoots(booksDir, cacheDir);
  hostStoreUseFile((std::string(cacheDir) + "/settings.txt").c_str());

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
         "book 0, c to show the control bar, q to quit.\n");

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
        case SDLK_c: simKey('c'); break;
        case SDLK_ESCAPE: case SDLK_q: goto quit;
      }
    }
  }
quit:
  SDL_Quit();
  return 0;
}
