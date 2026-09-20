#include "net/UploadServer.h"

#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "hal/Storage.h"

namespace {

WebServer gServer(80);
File gUploadFile;
bool gUploadFailed = false;
const char* gUploadError = "";

// Refuse an upload below this much free space. A book costs its own size
// again in decompressed chapters, so accepting one that barely fits would
// only fail later during import.
constexpr uint64_t MIN_FREE_BYTES = 256 * 1024;

constexpr uint32_t STATION_TIMEOUT_MS = 12000;
constexpr const char* AP_SSID = "Kindle-Setup";
constexpr const char* AP_PASS = "readabook";

const char kPageHead[] PROGMEM =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Books</title><style>"
    "body{font:16px/1.5 system-ui,sans-serif;max-width:40rem;margin:2rem auto;padding:0 1rem}"
    "h1{font-size:1.3rem}"
    "form{border:1px solid #ccc;border-radius:8px;padding:1rem;margin-bottom:1.5rem}"
    "li{display:flex;justify-content:space-between;align-items:center;padding:.4rem 0;"
    "border-bottom:1px solid #eee}"
    "ul{list-style:none;padding:0}a{color:#b00;text-decoration:none}"
    "button{padding:.5rem 1rem}"
    "</style></head><body><h1>Books on device</h1>"
    "<form method=POST action=/upload enctype=multipart/form-data>"
    "<input type=file name=book accept=.epub,.txt multiple required>"
    "<button type=submit>Upload</button></form>";

const char kPageTail[] PROGMEM = "</ul></body></html>";

bool hasBookExtension(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".epub") == 0 || strcasecmp(dot, ".txt") == 0;
}

// Keeps uploaded names safe to use as SD paths.
void sanitiseName(const char* in, char* out, size_t outLen) {
  const char* base = strrchr(in, '/');
  if (base) in = base + 1;
  base = strrchr(in, '\\');
  if (base) in = base + 1;

  size_t o = 0;
  for (; *in && o < outLen - 1; in++) {
    char c = *in;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == ' ';
    out[o++] = ok ? c : '_';
  }
  out[o] = '\0';
  if (o == 0) snprintf(out, outLen, "book.epub");
}

void handleRoot() {
  String page;
  page.reserve(2048);
  page += FPSTR(kPageHead);

  // Storage is internal flash, not a card, so the ceiling is low enough that
  // it is worth showing before someone tries to upload a third novel.
  page += "<p><b>";
  page += String(static_cast<uint32_t>(gStorage.freeBytes() / 1024));
  page += " KB free</b> of ";
  page += String(static_cast<uint32_t>(gStorage.totalBytes() / 1024));
  page += " KB. A book also needs about its own size again once imported.</p><ul>";

  File dir = gStorage.fs().open(DIR_BOOKS);
  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String name = entry.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);

        if (hasBookExtension(name.c_str())) {
          page += "<li><span>";
          page += name;
          page += "</span><span>";
          page += String(entry.size() / 1024);
          page += " KB &nbsp;<a href='/delete?name=";
          page += name;
          page += "'>delete</a></span></li>";
        }
      }
      entry.close();
    }
    dir.close();
  }

  page += FPSTR(kPageTail);
  gServer.send(200, "text/html", page);
}

void handleUploadData() {
  HTTPUpload& upload = gServer.upload();

  if (upload.status == UPLOAD_FILE_START) {
    gUploadFailed = false;
    gUploadError = "";

    char safe[80];
    sanitiseName(upload.filename.c_str(), safe, sizeof(safe));
    if (!hasBookExtension(safe)) {
      gUploadFailed = true;
      gUploadError = "Only .epub and .txt files are accepted";
      log_w("upload: rejected '%s'", safe);
      return;
    }

    // Check before writing rather than filling the partition and failing
    // mid-stream, which would leave a truncated book behind.
    if (gStorage.freeBytes() < MIN_FREE_BYTES) {
      gUploadFailed = true;
      gUploadError = "Not enough free space. Delete a book first.";
      log_w("upload: only %llu KB free", gStorage.freeBytes() / 1024);
      return;
    }

    char path[160];
    snprintf(path, sizeof(path), "%s/%s", DIR_BOOKS, safe);
    gStorage.fs().remove(path);

    gUploadFile = gStorage.fs().open(path, FILE_WRITE);
    if (!gUploadFile) {
      gUploadFailed = true;
      gUploadError = "Could not create the file";
      log_e("upload: cannot create %s", path);
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (gUploadFile && !gUploadFailed) {
      if (gUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        gUploadFailed = true;
        gUploadError = "Ran out of space partway through";
        log_e("upload: short write, storage is full");
      }
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
    if (gUploadFile) gUploadFile.close();
  }
}

}  // namespace

UploadServer gUploadServer;

void UploadServer::routes() {
  gServer.on("/", HTTP_GET, handleRoot);

  gServer.on(
      "/upload", HTTP_POST,
      [this]() {
        if (gUploadFailed) {
          gServer.send(507, "text/plain",
                       *gUploadError ? gUploadError : "Upload failed");
        } else {
          changes_++;
          gServer.sendHeader("Location", "/");
          gServer.send(303);
        }
      },
      handleUploadData);

  gServer.on("/delete", HTTP_GET, [this]() {
    if (!gServer.hasArg("name")) {
      gServer.send(400, "text/plain", "missing name");
      return;
    }
    char safe[80];
    sanitiseName(gServer.arg("name").c_str(), safe, sizeof(safe));

    char path[160];
    snprintf(path, sizeof(path), "%s/%s", DIR_BOOKS, safe);
    if (gStorage.fs().remove(path)) {
      // Drop the derived cache too, or the next book with this name would
      // inherit stale chapters.
      char cache[80];
      Storage::cacheDir(Storage::bookId(safe), cache, sizeof(cache));
      gStorage.removeTree(cache);
      changes_++;
    }
    gServer.sendHeader("Location", "/");
    gServer.send(303);
  });

  gServer.onNotFound([]() {
    gServer.sendHeader("Location", "/");
    gServer.send(303);
  });
}

bool UploadServer::startStation(const char* ssid, const char* password) {
  if (!ssid || !*ssid) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < STATION_TIMEOUT_MS) {
    delay(150);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
    return false;
  }

  snprintf(network_, sizeof(network_), "%s", ssid);
  snprintf(url_, sizeof(url_), "http://%s", WiFi.localIP().toString().c_str());
  mode_ = Mode::Station;
  return true;
}

void UploadServer::startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  snprintf(network_, sizeof(network_), "%s", AP_SSID);
  snprintf(url_, sizeof(url_), "http://%s", WiFi.softAPIP().toString().c_str());
  mode_ = Mode::AccessPoint;
}

bool UploadServer::begin(const char* ssid, const char* password) {
  if (mode_ != Mode::Off) return true;

  if (!startStation(ssid, password)) startAccessPoint();

  routes();
  gServer.begin();
  log_i("upload server on %s (%s)", url_, network_);
  return true;
}

void UploadServer::stop() {
  if (mode_ == Mode::Off) return;

  gServer.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  mode_ = Mode::Off;
  network_[0] = '\0';
  url_[0] = '\0';
}

void UploadServer::handle() {
  if (mode_ == Mode::Off) return;
  gServer.handleClient();
}
