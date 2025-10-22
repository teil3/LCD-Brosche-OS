#include "SlideshowApp.h"

#include <algorithm>

#include "Core/Gfx.h"
#include "Config.h"
#include "Core/TextRenderer.h"
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
  copyState_ = CopyState::Confirm;
  copyAbortRequest_ = false;
  copyConfirmSelection_ = 1; // Ja vorauswaehlen
  toastText_.clear();
  toastUntil_ = 0;
  toastDirty_ = false;
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
  if (!mountLittleFs(false)) {
    Serial.println("[Slideshow] LittleFS mount retry");
    if (!mountLittleFs(true)) {
      Serial.println("[Slideshow] LittleFS format+mount failed");
      return false;
    }
  }
  if (!ensureFlashSlidesDir()) {
    Serial.println("[Slideshow] ensureFlashSlidesDir failed");
    return false;
  }
  return true;
}

bool SlideshowApp::ensureSdReady_() {
  if (SD.cardType() != CARD_NONE) {
    return true;
  }

  digitalWrite(TFT_CS_PIN, HIGH);
  bool ok = SD.begin(SD_CS_PIN, sdSPI, 5000000);
  if (!ok) {
    Serial.println("[Slideshow] SD init retry @2MHz");
    ok = SD.begin(SD_CS_PIN, sdSPI, 2000000);
  }
  if (!ok) {
    Serial.println("[Slideshow] SD init failed");
  }
  return ok;
}

bool SlideshowApp::prepareCopyQueue_() {
  copyQueue_.clear();
  copyQueueIndex_ = 0;
  copyBytesDone_ = 0;
  copyBytesTotal_ = 0;
  copyFileBytesDone_ = 0;
  copyAbortRequest_ = false;
  closeCopyFiles_();

  if (!ensureSdReady_()) {
    return false;
  }
  std::vector<String> sdFiles;
  if (!readDirectoryEntries_(&SD, dir, sdFiles)) {
    Serial.printf("[Slideshow] prepareCopyQueue: readDirectoryEntries failed for '%s'\n", dir.c_str());
    return false;
  }

  std::vector<CopyItem> items;
  items.reserve(sdFiles.size());

  for (String path : sdFiles) {
    if (!path.startsWith("/")) path = "/" + path;
    String base = path;
    int s = base.lastIndexOf('/');
    if (s >= 0) base = base.substring(s + 1);
    if (!isJpeg_(base)) continue;

    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
      Serial.printf("[Slideshow] copy queue open fail: %s\n", path.c_str());
      continue;
    }
    size_t size = f.size();
    f.close();
    if (size == 0) continue;

    CopyItem ci;
    ci.path = path;
    ci.name = base;
    ci.size = size;
    items.push_back(ci);
  }

  if (items.empty()) {
    Serial.println("[Slideshow] prepareCopyQueue: no JPEG files on SD");
    return false;
  }

  if (!ensureFlashReady_()) {
    Serial.println("[Slideshow] prepareCopyQueue: ensureFlashReady_ failed");
    return false;
  }

  std::sort(items.begin(), items.end(), [](const CopyItem& a, const CopyItem& b) {
    return a.name < b.name;
  });

  if (!clearFlashSlidesDir()) {
    Serial.println("[Slideshow] WARN: Flash clear failed (continue)");
  }

  copyQueue_ = std::move(items);
  for (const auto& item : copyQueue_) {
    copyBytesTotal_ += item.size;
  }
  Serial.printf("[Slideshow] prepareCopyQueue: %u files, total=%u bytes\n",
                static_cast<unsigned>(copyQueue_.size()),
                static_cast<unsigned>(copyBytesTotal_));
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
  copyOverlayNeedsClear_ = true;
  copyHeaderChecksum_ = 0xFFFFFFFFu;
  copyBarFill_ = 0;
  copyHintDrawn_ = false;
  copyToastActive_ = false;
  copyLastToast_.clear();
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
  copyOverlayNeedsClear_ = true;
  copyHeaderChecksum_ = 0xFFFFFFFFu;
  copyBarFill_ = 0;
  copyHintDrawn_ = false;
  copyToastActive_ = false;
  copyLastToast_.clear();
  markCopyConfirmDirty_();

  if (!message.isEmpty()) {
    showToast_(message, (state == CopyState::Error) ? kCopyErrorToastMs : kToastLongMs);
  }

  if (state == CopyState::Done) {
    setSource_(SlideSource::Flash, false);
    showToast_("Fertig kopiert", kCopyDoneToastMs);
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
      String flashPath = String(kFlashSlidesDir) + "/" + item.name;

      copySrc_ = SD.open(item.path.c_str(), FILE_READ);
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
    int16_t y = TFT_H - TextRenderer::lineHeight() - 4;
    if (y < 0) y = 0;
    TextRenderer::drawCentered(y, base, TFT_WHITE, TFT_BLACK);
  }

  if (toastUntil_) {
    toastDirty_ = true;
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
      return String("MANUELL");
    case ControlMode::StorageMenu:
      return String("EINSTELLUNG");
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
  toastDirty_ = true;
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
    toastDirty_ = false;
    return;
  }
  if (!toastDirty_) return;
  toastDirty_ = false;

  int16_t textY = (TFT_H - TextRenderer::lineHeight()) / 2;
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, toastText_, TFT_WHITE, TFT_BLACK);
}

void SlideshowApp::drawCopyOverlay_() {
  if (copyState_ != CopyState::Running) return;

  const uint32_t now = millis();
  bool headerChanged = false;
  bool progressChanged = false;

  const size_t totalFiles = copyQueue_.size();
  const size_t displayedIndex = std::min(copyQueueIndex_ + (copySrc_ ? 1u : 0u), totalFiles);

  int16_t headerY = 52;
  int16_t barWidth = TFT_W - 40;
  int16_t barHeight = 20;
  int16_t barX = (TFT_W - barWidth) / 2;
  int16_t barY = (TFT_H / 2) - 6;

  uint32_t headerChecksum = ((static_cast<uint32_t>(displayedIndex) & 0xFFFF) << 16) |
                           (static_cast<uint32_t>(totalFiles) & 0xFFFF);
  float progress = (copyBytesTotal_ == 0) ? 0.0f : static_cast<float>(copyBytesDone_) / static_cast<float>(copyBytesTotal_);
  progress = std::min(std::max(progress, 0.0f), 1.0f);
  uint16_t progressFill = static_cast<uint16_t>((barWidth - 2) * progress);
  if (progressFill < copyBarFill_) {
    progressFill = copyBarFill_;
  }

  if (copyOverlayNeedsClear_) {
    tft.fillScreen(TFT_BLACK);
    copyOverlayNeedsClear_ = false;
    headerChanged = true;
    progressChanged = true;
  }

  if (headerChecksum != copyHeaderChecksum_) {
    headerChanged = true;
    copyHeaderChecksum_ = headerChecksum;
  }

  uint16_t fillDelta = (progressFill > copyBarFill_) ? (progressFill - copyBarFill_) : 0;
  if (progressFill != copyBarFill_) {
    progressChanged = true;
  }
  if (!progressChanged) {
    // force repaint each frame to avoid stale fill when queue started anew
    progressChanged = true;
  }

  if (headerChanged) {
    tft.fillRect(0, headerY - 6, TFT_W, TextRenderer::lineHeight() * 2 + 44, TFT_BLACK);
    String header = String("Kopiere ") + String(displayedIndex) + "/" + String(totalFiles);
    TextRenderer::drawCentered(headerY, header, TFT_WHITE, TFT_BLACK);
  }

  if (progressChanged) {
    tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
    tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, TFT_BLACK);
    if (progressFill > 0) {
      tft.fillRect(barX + 1, barY + 1, progressFill, barHeight - 2, TFT_WHITE);
    }
    copyBarFill_ = progressFill;
  }

  const int16_t textTop = barY + barHeight + 10;
  const int16_t hintY = textTop + TextRenderer::lineHeight() - 15;
  const bool toastActive = toastUntil_ && now < toastUntil_;
  const bool toastChanged = (toastActive != copyToastActive_) || (toastActive && toastText_ != copyLastToast_);

  if (toastChanged) {
    tft.fillRect(0, textTop - 4, TFT_W, TextRenderer::lineHeight() * 2 + 24, TFT_BLACK);
    copyHintDrawn_ = false;
  }

  if (toastActive) {
    if (toastChanged || !copyToastActive_) {
      TextRenderer::drawCentered(textTop, toastText_, TFT_WHITE, TFT_BLACK);
    }
    copyLastToast_ = toastText_;
  } else if (toastChanged) {
    copyLastToast_.clear();
  }

  if (!copyHintDrawn_) {
    TextRenderer::drawCentered(hintY, "Lang: Abbrechen", TFT_WHITE, TFT_BLACK);
    copyHintDrawn_ = true;
  }

  copyToastActive_ = toastActive;
}

void SlideshowApp::drawCopyConfirmOverlay_() {
  if (!copyConfirmDirty_ && copyConfirmLastSelection_ == copyConfirmSelection_) {
    return;
  }

  copyConfirmDirty_ = false;
  copyConfirmLastSelection_ = copyConfirmSelection_;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 26;
  TextRenderer::drawCentered(top, "SD", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line, "kopieren", TFT_WHITE, TFT_BLACK);

  String options = (copyConfirmSelection_ == 0) ? String("> Nein    Ja")
                                               : String("Nein    > Ja");
  TextRenderer::drawCentered(top + line * 2 + 16, options, TFT_WHITE, TFT_BLACK);

  TextRenderer::drawCentered(top + line * 3 + 28, "Klick: Wechseln", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line * 4 + 28, "Lang: Bestätigen", TFT_WHITE, TFT_BLACK);
}

void SlideshowApp::drawStorageMenuOverlay_() {
  bool toastActive = toastUntil_ && millis() < toastUntil_;
  if (!toastActive && toastUntil_) {
    toastUntil_ = 0;
    toastText_.clear();
    toastActive = false;
  }

  String sourceLine = String("Quelle: ") + sourceLabel_();
  String footerLine = toastActive ? toastText_ : String("Lang: Modus");

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

  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  const int16_t top = 22;
  const int16_t sourceY = top + line + spacing;
  const int16_t singleY = sourceY + line + spacing;
  const int16_t doubleY = singleY + line + spacing;
  const int16_t footerY = doubleY + line + spacing;

  TextRenderer::drawCentered(top, "Setup", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(sourceY, sourceLine, TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(singleY, "Klick: Quelle", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(doubleY, "Doppel: Kopieren", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(footerY, footerLine, TFT_WHITE, TFT_BLACK);
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
  if (source_ == SlideSource::SDCard && !ensureSdReady_()) {
    Serial.println("[Slideshow] rebuild: SD not ready");
  }
  if (rebuildFileListFrom_(source_)) {
    idx_ = 0;
    return true;
  }

  if (source_ == SlideSource::SDCard) {
    Serial.println("[Slideshow] SD leer – auf Flash umschalten");
    if (rebuildFileListFrom_(SlideSource::Flash)) {
      source_ = SlideSource::Flash;
      idx_ = 0;
      return true;
    }
  }

  if (source_ == SlideSource::Flash && ensureSdReady_() && rebuildFileListFrom_(SlideSource::SDCard)) {
    source_ = SlideSource::SDCard;
    idx_ = 0;
    return true;
  }

  return false;
}

void SlideshowApp::init() {
  files_.clear();
  idx_ = 0;
  timeSinceSwitch_ = 0;
  toastText_.clear();
  toastUntil_ = 0;
  toastDirty_ = false;
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
    toastDirty_ = false;
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
        showToast_("Lang: Modus verlassen", kToastShortMs);
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
