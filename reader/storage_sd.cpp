// Book storage on a microSD card (FAT32), wired as in board_config.h.
//
// Paths map straight onto the card: "/books/x.epub" is the file x.epub in
// the card's "books" folder, and the reader's cache goes in "/.reader".
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "board_config.h"
#include "storage.h"

static SPIClass sdSpi(HSPI);
static const uint32_t SD_CLOCK_HZ = 20000000;

struct StorageFile {
  File file;
};

bool storageBegin() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  // Start the bus on our pins first: SD.begin would otherwise start it on
  // the default ones, one of which (19) is the front light.
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSpi, SD_CLOCK_HZ)) {
    Serial.println("storage: no SD card found. Check the wiring (see board_config.h) "
                   "and that the card is formatted FAT32.");
    return false;
  }
  if (!SD.exists("/books")) SD.mkdir("/books");
  if (!SD.exists("/.reader")) SD.mkdir("/.reader");
  Serial.printf("storage: SD card, %llu MB\n", SD.cardSize() / (1024 * 1024));
  return true;
}

StorageFile *storageOpen(const char *path, bool write) {
  File f = SD.open(path, write ? FILE_WRITE : FILE_READ);
  if (!f) return nullptr;
  if (f.isDirectory()) {
    f.close();
    return nullptr;
  }
  return new StorageFile{ f };
}

int32_t storageRead(StorageFile *f, void *buf, uint32_t n) {
  return (int32_t)f->file.read((uint8_t *)buf, n);
}

bool storageWrite(StorageFile *f, const void *buf, uint32_t n) {
  return f->file.write((const uint8_t *)buf, n) == n;
}

bool storageSeek(StorageFile *f, uint32_t pos) {
  return f->file.seek(pos);
}

uint32_t storageSize(StorageFile *f) {
  return (uint32_t)f->file.size();
}

void storageClose(StorageFile *f) {
  if (!f) return;
  f->file.close();
  delete f;
}

bool storageExists(const char *path) {
  return SD.exists(path);
}

bool storageMkdir(const char *path) {
  return SD.exists(path) || SD.mkdir(path);
}

bool storageRename(const char *from, const char *to) {
  return SD.rename(from, to);
}

bool storageRemove(const char *path) {
  return SD.remove(path);
}

bool storageList(const char *dir, StorageListFn fn, void *ctx) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return false;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    fn(e.name(), (uint32_t)e.size(), e.isDirectory(), ctx);
    e.close();
  }
  d.close();
  return true;
}
