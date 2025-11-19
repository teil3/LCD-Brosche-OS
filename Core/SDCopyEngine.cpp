#include "SDCopyEngine.h"
#include "Storage.h"
#include "Config.h"
#include <algorithm>

SDCopyEngine::SDCopyEngine() {
}

SDCopyEngine::~SDCopyEngine() {
  reset();
}

bool SDCopyEngine::prepare() {
  // Reset state
  queue_.clear();
  queueIndex_ = 0;
  bytesDone_ = 0;
  bytesTotal_ = 0;
  fileBytesDone_ = 0;
  abortRequest_ = false;
  closeFiles_();
  state_ = State::Idle;
  outcome_ = Outcome::None;
  outcomeMessage_.clear();

  // Allocate buffer
  if (buffer_ == nullptr) {
    buffer_ = (uint8_t*)malloc(kBufferSize);
    if (buffer_ == nullptr) {
      finalize_(Outcome::Error, "Out of memory");
      return false;
    }
  }

  // Ensure SD and Flash are ready
  if (!ensureSdReady_()) {
    finalize_(Outcome::Error, "SD card not ready");
    return false;
  }
  if (!ensureFlashReady_()) {
    finalize_(Outcome::Error, "LittleFS not ready");
    return false;
  }

  // Ensure required directories exist
  if (!LittleFS.exists("/system")) {
    if (!LittleFS.mkdir("/system")) {
      finalize_(Outcome::Error, "Failed to create /system");
      return false;
    }
  }
  if (!LittleFS.exists("/system/fonts")) {
    if (!LittleFS.mkdir("/system/fonts")) {
      finalize_(Outcome::Error, "Failed to create /system/fonts");
      return false;
    }
  }
  if (!LittleFS.exists("/scripts")) {
    if (!LittleFS.mkdir("/scripts")) {
      finalize_(Outcome::Error, "Failed to create /scripts");
      return false;
    }
  }

  // Scan SD card root for files to copy
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    finalize_(Outcome::Error, "Cannot open SD root");
    return false;
  }

  std::vector<CopyItem> items;
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    String filename = entry.name();
    int slash = filename.lastIndexOf('/');
    if (slash >= 0) {
      filename = filename.substring(slash + 1);
    }

    String lower = filename;
    lower.toLowerCase();
    size_t size = entry.size();

    if (size == 0) {
      entry.close();
      continue;
    }

    CopyItem ci;
    ci.path = String("/") + filename;
    ci.name = filename;
    ci.size = size;
    bool accepted = false;

    // Bootlogo
    if (lower == "bootlogo.jpg" || lower == "boot.jpg") {
      ci.type = FileType::Bootlogo;
      ci.destPath = "/system/bootlogo.jpg";
      accepted = true;
    }
    // textapp.cfg (only if not exists)
    else if (lower == "textapp.cfg") {
      if (!LittleFS.exists("/textapp.cfg")) {
        ci.type = FileType::Config;
        ci.destPath = "/textapp.cfg";
        accepted = true;
      }
    }
    // i18n.json and language-specific i18n files
    else if (lower == "i18n.json") {
      ci.type = FileType::Config;
      ci.destPath = "/system/i18n.json";
      accepted = true;
    } else if (lower.startsWith("i18n_") && lower.endsWith(".json")) {
      ci.type = FileType::Config;
      ci.destPath = String("/system/") + lower;
      accepted = true;
    }
    // Main fonts: font.vlw, fontsmall.vlw
    else if (lower == "font.vlw" || lower == "fontsmall.vlw") {
      if (size <= 30000) {
        ci.type = FileType::Font;
        ci.destPath = (lower == "fontsmall.vlw") ? "/system/fontsmall.vlw" : "/system/font.vlw";
        accepted = true;
      }
    }
    // Other .vlw fonts
    else if (lower.endsWith(".vlw")) {
      if (size <= 30720) {
        ci.type = FileType::Font;
        ci.destPath = String("/system/fonts/") + lower;
        accepted = true;
      }
    }
    // Lua scripts
    else if (lower.endsWith(".lua")) {
      ci.type = FileType::Lua;
      ci.destPath = String("/scripts/") + filename;
      accepted = true;
    }
    // JPEG images
    else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
      ci.type = FileType::Jpg;
      String destName = filename;
      String ext = ".jpg";
      int dot = destName.lastIndexOf('.');
      String base = destName;
      if (dot >= 0) {
        ext = destName.substring(dot);
        base = destName.substring(0, dot);
      }
      ci.destPath = String(kFlashSlidesDir) + "/" + destName;

      // Auto-rename if file exists
      int counter = 1;
      while (LittleFS.exists(ci.destPath) && counter < 1000) {
        destName = base + "-" + String(counter) + ext;
        ci.destPath = String(kFlashSlidesDir) + "/" + destName;
        ++counter;
      }
      accepted = true;
    }
    // GIF images
    else if (lower.endsWith(".gif")) {
      ci.type = FileType::Gif;
      String destName = filename;
      String ext = ".gif";
      int dot = destName.lastIndexOf('.');
      String base = destName;
      if (dot >= 0) {
        ext = destName.substring(dot);
        base = destName.substring(0, dot);
      }
      ci.destPath = String(kFlashSlidesDir) + "/" + destName;

      // Auto-rename if file exists
      int counter = 1;
      while (LittleFS.exists(ci.destPath) && counter < 1000) {
        destName = base + "-" + String(counter) + ext;
        ci.destPath = String(kFlashSlidesDir) + "/" + destName;
        ++counter;
      }
      accepted = true;
    }

    entry.close();

    if (accepted) {
      items.push_back(std::move(ci));
    }
  }

  root.close();

  if (items.empty()) {
    finalize_(Outcome::Error, "No files found");
    return false;
  }

  // Sort by type, then by name
  std::sort(items.begin(), items.end(), [](const CopyItem& a, const CopyItem& b) {
    if (a.type != b.type) {
      return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
    }
    return a.name < b.name;
  });

  // Calculate total bytes
  queue_ = std::move(items);
  for (const auto& item : queue_) {
    bytesTotal_ += item.size;
  }

  state_ = State::Running;
  return true;
}

void SDCopyEngine::tick() {
  if (state_ != State::Running) {
    return;
  }

  // Safety check: buffer must be allocated
  if (buffer_ == nullptr) {
    finalize_(Outcome::Error, "Buffer not allocated");
    return;
  }

  const uint32_t start = millis();

  while (state_ == State::Running && (millis() - start) < kTickBudgetMs) {
    // Check abort request
    if (abortRequest_) {
      finalize_(Outcome::Aborted, "Aborted by user");
      return;
    }

    // Open next file if needed
    if (!srcFile_) {
      if (queueIndex_ >= queue_.size()) {
        finalize_(Outcome::Success, "");
        return;
      }

      const CopyItem& item = queue_[queueIndex_];
      srcFile_ = SD.open(item.path.c_str(), FILE_READ);
      dstFile_ = LittleFS.open(item.destPath.c_str(), FILE_WRITE);
      fileBytesDone_ = 0;

      if (!srcFile_ || !dstFile_) {
        String msg = "Error at: " + item.name;
        finalize_(Outcome::Error, msg);
        return;
      }
    }

    // Copy chunk
    size_t available = srcFile_.available();
    if (available == 0) {
      // File complete, close and move to next
      srcFile_.close();
      dstFile_.close();
      ++queueIndex_;
      continue;
    }

    size_t toRead = std::min(kBufferSize, available);
    size_t n = srcFile_.read(buffer_, toRead);

    if (n == 0) {
      finalize_(Outcome::Error, "Read error");
      return;
    }

    if (dstFile_.write(buffer_, n) != n) {
      finalize_(Outcome::Error, "Write error");
      return;
    }

    bytesDone_ += n;
    fileBytesDone_ += n;
  }
}

void SDCopyEngine::abort() {
  abortRequest_ = true;
}

void SDCopyEngine::reset() {
  closeFiles_();
  queue_.clear();
  queueIndex_ = 0;
  bytesTotal_ = 0;
  bytesDone_ = 0;
  fileBytesDone_ = 0;
  abortRequest_ = false;
  state_ = State::Idle;
  outcome_ = Outcome::None;
  outcomeMessage_.clear();

  if (buffer_ != nullptr) {
    free(buffer_);
    buffer_ = nullptr;
  }
}

String SDCopyEngine::getCurrentFileName() const {
  if (queueIndex_ < queue_.size()) {
    return queue_[queueIndex_].name;
  }
  return "";
}

int SDCopyEngine::getPercentDone() const {
  if (bytesTotal_ == 0) {
    return 0;
  }
  return (bytesDone_ * 100) / bytesTotal_;
}

bool SDCopyEngine::ensureSdReady_() {
  if (SD.cardType() != CARD_NONE) {
    return true;
  }

  digitalWrite(TFT_CS_PIN, HIGH);
  bool ok = SD.begin(SD_CS_PIN, sdSPI, 5000000);
  if (!ok) {
    ok = SD.begin(SD_CS_PIN, sdSPI, 2000000);
  }
  return ok;
}

bool SDCopyEngine::ensureFlashReady_() {
  if (!mountLittleFs(false)) {
    if (!mountLittleFs(true)) {
      return false;
    }
  }
  if (!ensureFlashSlidesDir()) {
    return false;
  }
  return true;
}

void SDCopyEngine::closeFiles_() {
  if (srcFile_) srcFile_.close();
  if (dstFile_) dstFile_.close();
}

void SDCopyEngine::finalize_(Outcome outcome, const String& message) {
  closeFiles_();
  queue_.clear();
  queueIndex_ = 0;
  bytesTotal_ = 0;
  bytesDone_ = 0;
  fileBytesDone_ = 0;
  abortRequest_ = false;

  if (buffer_ != nullptr) {
    free(buffer_);
    buffer_ = nullptr;
  }

  state_ = State::Finished;
  outcome_ = outcome;
  outcomeMessage_ = message;
}
