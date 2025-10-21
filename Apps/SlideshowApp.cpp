#include "SlideshowApp.h"

#include <algorithm>

#include "Core/Gfx.h"
#include "Config.h"
#include "Core/TinyFont.h"
#include "Core/Storage.h"

namespace {
constexpr std::array<uint32_t, 5> kDwellSteps{1000, 5000, 10000, 30000, 300000};
constexpr uint32_t kToastShortMs = 1000;
constexpr uint32_t kToastLongMs = 1500;
constexpr uint32_t kCopyTickBudgetMs = 25;   // max Arbeit je loop
constexpr uint32_t kCopyAbortToastMs = 1200;
constexpr uint32_t kCopyDoneToastMs = 1800;
constexpr uint32_t kCopyErrorToastMs = 1800;
}

bool SlideshowApp::isJpeg_(const String& n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

void SlideshowApp::setControlMode_(ControlMode mode, bool showToast) {
  if (copyState_ == CopyState::Running) return;
  ControlMode previous = controlMode_;
  if (previous == mode) return;

  controlMode_ = mode;
  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  }
  switch (controlMode_) {
    case ControlMode::Auto:
      auto_mode = true;
      if (showToast) showToast_(modeLabel_(), kToastShortMs);
      break;

    case ControlMode::Manual:
      auto_mode = false;
      if (showToast) showToast_(modeLabel_(), kToastShortMs);
      break;

    case ControlMode::StorageMenu:
      auto_mode = false;
      toastText_.clear();
      toastUntil_ = 0;
      storageMenuLastFooter_.clear();
      storageMenuLastSource_.clear();
      storageMenuLastToastActive_ = false;
      break;
  }
  timeSinceSwitch_ = 0;

  if (previous == ControlMode::StorageMenu && controlMode_ != ControlMode::StorageMenu) {
    if (!files_.empty()) {
      showCurrent_();
    }
  }
}

void SlideshowApp::setSource_(SlideSource src, bool showToast) {
  if (source_ == src && !files_.empty()) {
    if (showToast) showToast_(String("Quelle: ") + sourceLabel_(), kToastShortMs);
    return;
  }

  source_ = src;
  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  }
  if (!rebuildFileList_()) {
    if (controlMode_ == ControlMode::StorageMenu) {
      toastText_.clear();
      toastUntil_ = 0;
      markStorageMenuDirty_();
    } else {
      showToast_(String("Keine Bilder: ") + sourceLabel_(), kToastLongMs);
    }
    tft.fillScreen(TFT_BLACK);
    return;
  }

  idx_ = 0;
  timeSinceSwitch_ = 0;
  showCurrent_();
  if (showToast) {
    showToast_(String("Quelle: ") + sourceLabel_(), kToastShortMs);
  }
}

void SlideshowApp::toggleSource_() {
  SlideSource next = (source_ == SlideSource::SDCard) ? SlideSource::Flash : SlideSource::SDCard;
  setSource_(next, true);
}

void SlideshowApp::enterStorageMenu_() {
  setControlMode_(ControlMode::StorageMenu);
}

void SlideshowApp::exitStorageMenu_() {
  if (controlMode_ != ControlMode::StorageMenu) return;
  setControlMode_(ControlMode::Auto);
}

void SlideshowApp::requestCopy_() {
  if (copyState_ == CopyState::Running) return;
  if (dir.isEmpty()) return;
  if (source_ != SlideSource::SDCard) {
    showToast_("Quelle zuerst SD waehlen", kToastShortMs);
    return;
  }

  copyState_ = CopyState::Confirm;
  copyAbortRequest_ = false;
  copyConfirmSelection_ = 1; // Ja vorauswaehlen
  toastText_.clear();
  toastUntil_ = 0;
  markCopyConfirmDirty_();
}

void SlideshowApp::cancelCopy_() {
  if (copyState_ == CopyState::Confirm) {
    copyState_ = CopyState::Idle;
    showToast_("Abgebrochen", kToastShortMs);
    markCopyConfirmDirty_();
  }
}

bool SlideshowApp::ensureFlashReady_() {
  return ensureFlashSlidesDir();
}

bool SlideshowApp::prepareCopyQueue_() {
  copyQueue_.clear();
  copyQueueIndex_ = 0;
  copyBytesDone_ = 0;
  copyBytesTotal_ = 0;
  copyFileBytesDone_ = 0;
  copyAbortRequest_ = false;
  closeCopyFiles_();

  if (!ensureFlashReady_()) {
    return false;
  }

  File root = SD.open(dir.c_str());
  if (!root || !root.isDirectory()) {
    return false;
  }

  std::vector<CopyItem> items;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    String base = f.name();
    int s = base.lastIndexOf('/');
    if (s >= 0) base = base.substring(s + 1);
    if (!isJpeg_(base)) continue;
    CopyItem ci;
    ci.name = base;
    ci.size = f.size();
    items.push_back(ci);
    copyBytesTotal_ += ci.size;
  }
  root.close();

  if (items.empty()) {
    return false;
  }

  std::sort(items.begin(), items.end(), [](const CopyItem& a, const CopyItem& b) {
    return a.name < b.name;
  });

  if (!clearFlashSlidesDir()) {
    Serial.println("[Slideshow] WARN: Flash clear failed (continue)");
  }

  copyQueue_ = std::move(items);
  return true;
}

void SlideshowApp::beginCopy_() {
  if (!prepareCopyQueue_()) {
    finalizeCopy_(CopyState::Error, "Keine Bilder gefunden");
    return;
  }

  copyState_ = CopyState::Running;
  auto_mode = false;
  controlMode_ = ControlMode::StorageMenu;
  tft.fillScreen(TFT_BLACK);
  drawCopyOverlay_();
}

void SlideshowApp::closeCopyFiles_() {
  if (copySrc_) copySrc_.close();
  if (copyDst_) copyDst_.close();
}

void SlideshowApp::finalizeCopy_(CopyState state, const String& message) {
  closeCopyFiles_();
  copyQueue_.clear();
  copyQueueIndex_ = 0;
  copyBytesTotal_ = 0;
  copyBytesDone_ = 0;
  copyFileBytesDone_ = 0;
  copyAbortRequest_ = false;
  copyState_ = CopyState::Idle;
  markCopyConfirmDirty_();

  if (!message.isEmpty()) {
    showToast_(message, (state == CopyState::Error) ? kCopyErrorToastMs : kToastLongMs);
  }

  if (state == CopyState::Done) {
    setSource_(SlideSource::Flash, false);
    showToast_("Kopie fertig – Quelle Flash", kCopyDoneToastMs);
  }
}

void SlideshowApp::handleCopyTick_(uint32_t budget_ms) {
  const uint32_t start = millis();

  while (copyState_ == CopyState::Running && (millis() - start) < budget_ms) {
    if (copyAbortRequest_) {
      finalizeCopy_(CopyState::Aborted, "Abgebrochen");
      return;
    }

    if (!copySrc_) {
      if (copyQueueIndex_ >= copyQueue_.size()) {
        finalizeCopy_(CopyState::Done, "");
        return;
      }

      const CopyItem& item = copyQueue_[copyQueueIndex_];
      String sdPath = dir;
      if (!sdPath.endsWith("/")) sdPath += "/";
      sdPath += item.name;

      String flashPath = String(kFlashSlidesDir) + "/" + item.name;

      copySrc_ = SD.open(sdPath.c_str(), FILE_READ);
      copyDst_ = LittleFS.open(flashPath.c_str(), FILE_WRITE);
      copyFileBytesDone_ = 0;

      if (!copySrc_ || !copyDst_) {
        finalizeCopy_(CopyState::Error, String("Fehler bei ") + item.name);
        return;
      }
    }

    const size_t chunkSize = copyBuf_.size();
    size_t available = copySrc_.available();
    if (available == 0) {
      copySrc_.close();
      copyDst_.close();
      ++copyQueueIndex_;
      continue;
    }

    size_t toRead = std::min(chunkSize, available);
    size_t n = copySrc_.read(copyBuf_.data(), toRead);
    if (n == 0) {
      finalizeCopy_(CopyState::Error, "Lesefehler");
      return;
    }
    if (copyDst_.write(copyBuf_.data(), n) != n) {
      finalizeCopy_(CopyState::Error, "Schreibfehler");
      return;
    }

    copyBytesDone_ += n;
    copyFileBytesDone_ += n;
  }

}

void SlideshowApp::showCurrent_() {
  if (files_.empty()) return;

  const String& path = files_[idx_];
  fs::FS* fs = filesystemFor(source_);
  if (!fs) return;

  File test = fs->open(path, FILE_READ);
  if (!test) {
    Serial.printf("[Slideshow] open FAIL: %s\n", path.c_str());
    return;
  }
  uint8_t sig[2] = {0, 0};
  size_t n = test.read(sig, 2);
  test.close();
  if (n != 2 || sig[0] != 0xFF || sig[1] != 0xD8) {
    Serial.printf("[Slideshow] WARN SOI: %s\n", path.c_str());
    return;
  }

  tft.fillScreen(TFT_BLACK);

  int rc = 0;
  if (source_ == SlideSource::SDCard) {
    rc = TJpgDec.drawSdJpg(0, 0, path.c_str());
  } else {
    File jpg = fs->open(path.c_str(), FILE_READ);
    if (!jpg) {
      Serial.printf("[Slideshow] Flash open FAIL: %s\n", path.c_str());
      return;
    }
    size_t size = jpg.size();
    if (size == 0 || size > 400000) {
      jpg.close();
      Serial.printf("[Slideshow] Flash size issue (%u): %s\n", (unsigned)size, path.c_str());
      return;
    }
    std::vector<uint8_t> data;
    data.resize(size);
    size_t read = jpg.read(data.data(), size);
    jpg.close();
    if (read != size) {
      Serial.printf("[Slideshow] Flash read fail: %s\n", path.c_str());
      return;
    }
    rc = TJpgDec.drawJpg(0, 0, data.data(), read);
  }
  if (rc != 1) {
    Serial.printf("[Slideshow] draw fail (%d): %s\n", rc, path.c_str());
    return;
  }

  if (show_filename) {
    String base = path;
    int sidx = base.lastIndexOf('/');
    if (sidx >= 0) base = base.substring(sidx + 1);
    uint8_t scale = 2;
    int16_t y = TFT_H - TinyFont::glyphHeight(scale) - 4;
    if (y < 0) y = 0;
    TinyFont::drawStringOutlineCentered(tft, y, base, TFT_BLACK, TFT_WHITE, scale);
  }

  drawToastOverlay_();

  Serial.printf("[Slideshow] OK (%s): %s\n", slideSourceLabel(source_), path.c_str());

  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  }
}

void SlideshowApp::advance_(int step) {
  if (files_.empty()) return;

  const size_t total = files_.size();
  int64_t next = static_cast<int64_t>(idx_) + step;
  int64_t wrap = static_cast<int64_t>(total);
  next %= wrap;
  if (next < 0) next += wrap;

  idx_ = static_cast<size_t>(next);
  showCurrent_();
  timeSinceSwitch_ = 0;
}

void SlideshowApp::applyDwell_() {
  if (dwellIdx_ >= kDwellSteps.size()) dwellIdx_ = 0;
  dwell_ms = kDwellSteps[dwellIdx_];
}

String SlideshowApp::dwellLabel_() const {
  if (dwell_ms % 60000 == 0) {
    return String(dwell_ms / 60000) + "m";
  }
  return String((dwell_ms + 500) / 1000) + "s";
}

String SlideshowApp::modeLabel_() const {
  switch (controlMode_) {
    case ControlMode::Auto:
      return String("AUTO ") + dwellLabel_();
    case ControlMode::Manual:
      return String("MANUAL");
    case ControlMode::StorageMenu:
      return String("SETUP");
  }
  return String("?");
}

String SlideshowApp::sourceLabel_() const {
  return String(slideSourceLabel(source_));
}

String SlideshowApp::dwellToastLabel_() const {
  return String("Dauer ") + dwellLabel_();
}

void SlideshowApp::showToast_(const String& txt, uint32_t duration_ms) {
  toastText_ = txt;
  toastUntil_ = millis() + duration_ms;
  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  } else {
    drawToastOverlay_();
  }
}

void SlideshowApp::drawToastOverlay_() {
  if (!toastUntil_) return;
  if (millis() >= toastUntil_) {
    toastUntil_ = 0;
    toastText_.clear();
    return;
  }

  uint8_t scale = 3;
  int16_t textY = (TFT_H - TinyFont::glyphHeight(scale)) / 2;
  if (textY < 0) textY = 0;
  TinyFont::drawStringOutlineCentered(tft, textY, toastText_, TFT_BLACK, TFT_WHITE, scale);
}

void SlideshowApp::drawCopyOverlay_() {
  if (copyState_ != CopyState::Running) return;

  tft.fillRect(0, 0, TFT_W, TFT_H, TFT_BLACK);

  const size_t totalFiles = copyQueue_.size();
  const size_t displayedIndex = std::min(copyQueueIndex_ + (copySrc_ ? 1u : 0u), totalFiles);

  String header = String("Kopiere ") + String(displayedIndex) + "/" + String(totalFiles);
  TinyFont::drawStringOutlineCentered(tft, 60, header, TFT_BLACK, TFT_WHITE, 3);

  String hint = String("BTN2 Long = Abbrechen");
  TinyFont::drawStringOutlineCentered(tft, 140, hint, TFT_BLACK, TFT_WHITE, 2);

  int16_t barWidth = TFT_W - 40;
  int16_t barHeight = 20;
  int16_t barX = (TFT_W - barWidth) / 2;
  int16_t barY = (TFT_H / 2) + 2; // 8px weiter nach oben

  tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
  float progress = (copyBytesTotal_ == 0) ? 0.0f : static_cast<float>(copyBytesDone_) / static_cast<float>(copyBytesTotal_);
  progress = std::min(std::max(progress, 0.0f), 1.0f);
  int16_t fill = static_cast<int16_t>((barWidth - 2) * progress);
  if (fill > 0) {
    tft.fillRect(barX + 1, barY + 1, fill, barHeight - 2, TFT_WHITE);
  }

  if (toastUntil_ && millis() < toastUntil_) {
    TinyFont::drawStringOutlineCentered(tft, barY + barHeight + 20, toastText_, TFT_BLACK, TFT_WHITE, 2);
  }
}

void SlideshowApp::drawCopyConfirmOverlay_() {
  if (!copyConfirmDirty_ && copyConfirmLastSelection_ == copyConfirmSelection_) {
    return;
  }

  copyConfirmDirty_ = false;
  copyConfirmLastSelection_ = copyConfirmSelection_;

  tft.fillScreen(TFT_BLACK);

  TinyFont::drawStringOutlineCentered(tft, 30, "SD", TFT_BLACK, TFT_WHITE, 2);
  TinyFont::drawStringOutlineCentered(tft, 58, "kopieren", TFT_BLACK, TFT_WHITE, 2);

  String options = (copyConfirmSelection_ == 0) ? String("> Nein    Ja")
                                               : String("Nein    > Ja");
  TinyFont::drawStringOutlineCentered(tft, 100, options, TFT_BLACK, TFT_WHITE, 2);

  TinyFont::drawStringOutlineCentered(tft, 142, "Single: Wechseln", TFT_BLACK, TFT_WHITE, 2);
  TinyFont::drawStringOutlineCentered(tft, 168, "Long: Bestaetigen", TFT_BLACK, TFT_WHITE, 2);
}

void SlideshowApp::drawStorageMenuOverlay_() {
  bool toastActive = toastUntil_ && millis() < toastUntil_;
  if (!toastActive && toastUntil_) {
    toastUntil_ = 0;
    toastText_.clear();
    toastActive = false;
  }

  String sourceLine = String("Quelle: ") + sourceLabel_();
  String footerLine = toastActive ? toastText_ : String("Long: Modus");

  if (!storageMenuDirty_ &&
      storageMenuLastSource_ == sourceLine &&
      storageMenuLastFooter_ == footerLine &&
      storageMenuLastToastActive_ == toastActive) {
    return;
  }

  storageMenuDirty_ = false;
  storageMenuLastSource_ = sourceLine;
  storageMenuLastFooter_ = footerLine;
  storageMenuLastToastActive_ = toastActive;

  tft.fillScreen(TFT_BLACK);

  TinyFont::drawStringOutlineCentered(tft, 28, "Setup", TFT_BLACK, TFT_WHITE, 2);
  TinyFont::drawStringOutlineCentered(tft, 68, sourceLine, TFT_BLACK, TFT_WHITE, 2);

  TinyFont::drawStringOutlineCentered(tft, 118, "Single: Quelle", TFT_BLACK, TFT_WHITE, 2);
  TinyFont::drawStringOutlineCentered(tft, 146, "Double: Kopieren", TFT_BLACK, TFT_WHITE, 2);
  TinyFont::drawStringOutlineCentered(tft, 174, footerLine, TFT_BLACK, TFT_WHITE, 2);
}

void SlideshowApp::markStorageMenuDirty_() {
  storageMenuDirty_ = true;
}

void SlideshowApp::markCopyConfirmDirty_() {
  copyConfirmDirty_ = true;
  copyConfirmLastSelection_ = 255;
}

bool SlideshowApp::readDirectoryEntries_(fs::FS* fs, const String& basePath, std::vector<String>& out) {
  if (!fs) return false;

  File root = fs->open(basePath.c_str());
  if (!root || !root.isDirectory()) {
    return false;
  }

  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    String base = f.name();
    int s = base.lastIndexOf('/');
    if (s >= 0) base = base.substring(s + 1);
    if (!isJpeg_(base)) continue;

    String full = basePath;
    if (!full.endsWith("/")) full += "/";
    full += base;
    out.push_back(full);
  }
  root.close();
  std::sort(out.begin(), out.end());
  return true;
}

bool SlideshowApp::rebuildFileListFrom_(SlideSource src) {
  std::vector<String> tmp;
  fs::FS* fs = filesystemFor(src);
  String base = (src == SlideSource::SDCard) ? dir : String(kFlashSlidesDir);
  if (!readDirectoryEntries_(fs, base, tmp)) {
    files_.clear();
    return false;
  }

  files_ = std::move(tmp);
  return !files_.empty();
}

bool SlideshowApp::rebuildFileList_() {
  bool ok = rebuildFileListFrom_(source_);
  if (ok) {
    idx_ = 0;
  }
  return ok;
}

void SlideshowApp::init() {
  files_.clear();
  idx_ = 0;
  timeSinceSwitch_ = 0;
  toastText_.clear();
  toastUntil_ = 0;
  dwellIdx_ = 1;
  auto_mode = true;
  controlMode_ = ControlMode::Auto;
  source_ = SlideSource::SDCard;
  copyState_ = CopyState::Idle;
  copyAbortRequest_ = false;
  markCopyConfirmDirty_();
  storageMenuDirty_ = true;
  storageMenuLastSource_.clear();
  storageMenuLastFooter_.clear();
  storageMenuLastToastActive_ = false;

  applyDwell_();

  if (!rebuildFileList_()) {
    tft.fillScreen(TFT_BLACK);
    showToast_("Keine Bilder auf SD", kToastLongMs);
    return;
  }

  showCurrent_();
  timeSinceSwitch_ = 0;
  showToast_(modeLabel_(), kToastShortMs);
}

void SlideshowApp::tick(uint32_t delta_ms) {
  const uint32_t now = millis();

  if (copyState_ == CopyState::Running) {
    handleCopyTick_(kCopyTickBudgetMs);
    return;
  }

  if (toastUntil_ && now >= toastUntil_) {
    toastUntil_ = 0;
    toastText_.clear();
    if (controlMode_ == ControlMode::StorageMenu) {
      markStorageMenuDirty_();
    } else if (!files_.empty()) {
      showCurrent_();
    }
  }

  if (files_.empty()) return;

  if (!auto_mode) return;

  timeSinceSwitch_ += delta_ms;
  if (timeSinceSwitch_ >= dwell_ms) {
    advance_(+1);
  }
}

void SlideshowApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (copyState_) {
    case CopyState::Running:
      if (e == BtnEvent::Long) {
        copyAbortRequest_ = true;
        showToast_("Abbruch angefordert", kCopyAbortToastMs);
      }
      return;

    case CopyState::Confirm:
      if (e == BtnEvent::Single) {
        copyConfirmSelection_ ^= 1;
        markCopyConfirmDirty_();
      } else if (e == BtnEvent::Long) {
        if (copyConfirmSelection_ == 1) {
          beginCopy_();
        } else {
          cancelCopy_();
        }
      }
      return;

    default:
      break;
  }

  switch (e) {
    case BtnEvent::Single:
      if (controlMode_ == ControlMode::StorageMenu) {
        toggleSource_();
      } else {
        advance_(+1);
      }
      break;

    case BtnEvent::Double:
      if (controlMode_ == ControlMode::StorageMenu) {
        requestCopy_();
      } else {
        dwellIdx_ = (dwellIdx_ + 1) % kDwellSteps.size();
        applyDwell_();
        timeSinceSwitch_ = 0;
        showToast_(dwellToastLabel_(), kToastShortMs);
        Serial.printf("[Slideshow] dwell=%lu ms\n", (unsigned long)dwell_ms);
      }
      break;

    case BtnEvent::Triple:
      if (controlMode_ == ControlMode::StorageMenu) {
        showToast_("Long: Modus verlassen", kToastShortMs);
      } else {
        show_filename = !show_filename;
        showCurrent_();
      }
      break;

    case BtnEvent::Long: {
      if (controlMode_ == ControlMode::Auto) {
        setControlMode_(ControlMode::Manual);
      } else if (controlMode_ == ControlMode::Manual) {
        setControlMode_(ControlMode::StorageMenu);
      } else {
        setControlMode_(ControlMode::Auto);
      }
      break;
    }

    default:
      break;
  }
}

void SlideshowApp::draw() {
  if (copyState_ == CopyState::Running) {
    drawCopyOverlay_();
    return;
  }
  if (copyState_ == CopyState::Confirm) {
    drawCopyConfirmOverlay_();
    return;
  }
  if (controlMode_ == ControlMode::StorageMenu) {
    drawStorageMenuOverlay_();
    return;
  }
  drawToastOverlay_();
}

void SlideshowApp::shutdown() {
  files_.clear();
  closeCopyFiles_();
  copyQueue_.clear();
}
