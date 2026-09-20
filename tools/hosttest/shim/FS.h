// Minimal stand-in for the Arduino FS API so the ZIP reader, EPUB importer
// and page index can be exercised against a real directory on the host.
#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <Arduino.h>

#define FILE_READ "rb"
#define FILE_WRITE "wb"
#define FILE_APPEND "ab"

class File {
 public:
  File() = default;
  File(FILE* f, std::string path, bool dir = false)
      : f_(f), path_(std::move(path)), dir_(dir) {}

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  File(File&& o) noexcept { moveFrom(std::move(o)); }
  File& operator=(File&& o) noexcept {
    if (this != &o) {
      close();
      moveFrom(std::move(o));
    }
    return *this;
  }

  ~File() { close(); }

  explicit operator bool() const { return f_ != nullptr || dir_; }

  int read(uint8_t* dst, size_t len) {
    if (!f_) return -1;
    return static_cast<int>(fread(dst, 1, len, f_));
  }

  size_t write(const uint8_t* src, size_t len) {
    if (!f_) return 0;
    return fwrite(src, 1, len, f_);
  }

  bool seek(uint32_t offset) {
    if (!f_) return false;
    return fseek(f_, static_cast<long>(offset), SEEK_SET) == 0;
  }

  size_t size() const {
    if (!f_) return 0;
    long cur = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long end = ftell(f_);
    fseek(f_, cur, SEEK_SET);
    return static_cast<size_t>(end);
  }

  bool isDirectory() const { return dir_; }
  const char* name() const { return path_.c_str(); }
  const char* path() const { return path_.c_str(); }

  File openNextFile();

  void close() {
    if (f_) {
      fclose(f_);
      f_ = nullptr;
    }
    if (dirHandle_) {
      closedir(dirHandle_);
      dirHandle_ = nullptr;
    }
  }

 private:
  void moveFrom(File&& o) {
    f_ = o.f_;
    path_ = std::move(o.path_);
    dir_ = o.dir_;
    dirHandle_ = o.dirHandle_;
    hostRoot_ = std::move(o.hostRoot_);
    o.f_ = nullptr;
    o.dirHandle_ = nullptr;
    o.dir_ = false;
  }

  FILE* f_ = nullptr;
  std::string path_;
  bool dir_ = false;
  DIR* dirHandle_ = nullptr;
  std::string hostRoot_;

  friend class HostFS;
};

inline File File::openNextFile() {
  if (!dirHandle_) return File();

  struct dirent* de;
  while ((de = readdir(dirHandle_)) != nullptr) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

    std::string devicePath = path_;
    if (devicePath.empty() || devicePath.back() != '/') devicePath += "/";
    devicePath += de->d_name;

    std::string full = hostRoot_ + devicePath;
    struct stat st;
    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      File f(nullptr, devicePath, true);
      f.dirHandle_ = opendir(full.c_str());
      f.hostRoot_ = hostRoot_;
      return f;
    }

    FILE* fp = fopen(full.c_str(), "rb");
    if (!fp) continue;
    File f(fp, devicePath);
    f.hostRoot_ = hostRoot_;
    return f;
  }
  return File();
}

namespace fs {

class FS {
 public:
  virtual ~FS() {}
  virtual File open(const char* path, const char* mode = FILE_READ) = 0;
  virtual bool exists(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool mkdir(const char* path) = 0;
  virtual bool rmdir(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
};

}  // namespace fs

// Maps the device's absolute paths onto a directory on the host.
class HostFS : public fs::FS {
 public:
  void setRoot(const char* root) { root_ = root; }

  std::string real(const char* path) const { return root_ + path; }

  File open(const char* path, const char* mode = FILE_READ) override {
    std::string full = real(path);

    struct stat st;
    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      File f(nullptr, path, true);
      f.dirHandle_ = opendir(full.c_str());
      f.hostRoot_ = root_;
      return f;
    }

    FILE* fp = fopen(full.c_str(), mode);
    if (!fp) return File();
    return File(fp, path);
  }

  bool exists(const char* path) override {
    struct stat st;
    return stat(real(path).c_str(), &st) == 0;
  }

  bool remove(const char* path) override { return ::remove(real(path).c_str()) == 0; }
  bool mkdir(const char* path) override { return ::mkdir(real(path).c_str(), 0755) == 0; }
  bool rmdir(const char* path) override { return ::rmdir(real(path).c_str()) == 0; }

  bool rename(const char* from, const char* to) override {
    return ::rename(real(from).c_str(), real(to).c_str()) == 0;
  }

 private:
  std::string root_;
};

extern HostFS SD;
