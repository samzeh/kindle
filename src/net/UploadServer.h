#pragma once
#include <Arduino.h>

// Browser-based book transfer over WiFi.
//
// Uses the synchronous WebServer rather than ESPAsyncWebServer: with WiFi up
// there is only about 160KB of heap left next to the framebuffer, and uploads
// stream straight to the SD card in small chunks so nothing is buffered.
class UploadServer {
 public:
  enum class Mode : uint8_t { Off, Station, AccessPoint };

  // Joins the configured network, falling back to its own access point when
  // no credentials are stored or the join times out.
  bool begin(const char* ssid, const char* password);
  void stop();
  void handle();

  bool running() const { return mode_ != Mode::Off; }
  Mode mode() const { return mode_; }
  const char* networkName() const { return network_; }
  const char* url() const { return url_; }

  // Increments on every completed upload or delete, so the library view knows
  // to rescan.
  uint32_t changeCount() const { return changes_; }

 private:
  bool startStation(const char* ssid, const char* password);
  void startAccessPoint();
  void routes();

  Mode mode_ = Mode::Off;
  char network_[40] = {0};
  char url_[40] = {0};
  uint32_t changes_ = 0;
};

extern UploadServer gUploadServer;
