#include "SlideshowApp.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Storage.h"
#include "Core/TextRenderer.h"

namespace {
constexpr std::array<uint32_t, 5> kDwellSteps{1000, 5000, 10000, 30000, 300000};
constexpr uint32_t kToastShortMs = 1000;
constexpr uint32_t kToastLongMs = 1500;
constexpr uint32_t kManualFilenameDurationMs = 2000;
}

bool SlideshowApp::isJpeg_(const String& n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

bool SlideshowApp::focusTransferredFile(const char* filename, size_t size) {
  if (!ensureFlashReady_()) {
    showToast_("Flash-Fehler", 1800);
    return false;
  }
  if (!rebuildFileListFrom_(SlideSource::Flash)) {
    showToast_("Keine Flash-Bilder", 1500);
    return false;
  }
  source_ = SlideSource::Flash;
  String target = filename ? String(filename) : String();
  target.toLowerCase();

  idx_ = 0;
  if (!target.isEmpty()) {
    for (size_t i = 0; i < files_.size(); ++i) {
      String candidate = files_[i];
      int s = candidate.lastIndexOf('/');
      if (s >= 0) candidate = candidate.substring(s + 1);
      candidate.toLowerCase();
      if (candidate == target) {
        idx_ = i;
        break;
      }
    }
  }

  timeSinceSwitch_ = 0;
  showCurrent_();

  if (filename && filename[0]) {
    String msg = String("Transfer fertig: ") + filename;
    if (size > 0) {
      uint32_t kb = (static_cast<uint32_t>(size) + 1023) / 1024;
      msg += " (";
      msg += kb;
      msg += " KB)";
    }
    showToast_(msg, 1500);
  }
  return true;
}

void SlideshowApp::setControlMode_(ControlMode mode, bool showToast) {
  ControlMode previous = controlMode_;
  if (previous == mode) return;

  controlMode_ = mode;
  if (controlMode_ != ControlMode::Manual) {
    manualFilenameActive_ = false;
    manualFilenameLabel_.clear();
    manualFilenameUntil_ = 0;
  }

  switch (controlMode_) {
    case ControlMode::Auto:
      auto_mode = true;
      if (showToast) {
        showToast_(modeLabel_(), kToastShortMs);
      }
      break;

    case ControlMode::Manual:
      auto_mode = false;
      if (showToast) {
        showToast_(modeLabel_(), kToastShortMs);
      }
      break;

    case ControlMode::DeleteMenu:
      auto_mode = false;
      toastText_.clear();
      toastUntil_ = 0;
      toastDirty_ = false;
      deleteState_ = DeleteState::Idle;
      deleteMenuSelection_ = 0;
      markDeleteMenuDirty_();
      break;
  }

  timeSinceSwitch_ = 0;

  if (controlMode_ != ControlMode::DeleteMenu) {
    if (!files_.empty()) {
      showCurrent_();
    } else {
      tft.fillScreen(TFT_BLACK);
      const int16_t line = TextRenderer::lineHeight();
      const int16_t centerY = (TFT_H - line * 2) / 2;
      TextRenderer::drawCentered(centerY, "Keine Bilder", TFT_WHITE, TFT_BLACK);
      TextRenderer::drawCentered(centerY + line, "gefunden", TFT_WHITE, TFT_BLACK);
    }
  }
}

void SlideshowApp::setSource_(SlideSource src, bool showToast) {
  if (source_ == src && !files_.empty()) {
    if (showToast) showToast_(String("Quelle: ") + sourceLabel_(), kToastShortMs);
    return;
  }

  source_ = src;
  if (!rebuildFileList_()) {
    showToast_(String("Keine Bilder: ") + sourceLabel_(), kToastLongMs);
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

bool SlideshowApp::setSlideSource(SlideSource src, bool showToast, bool renderNow) {
  if (source_ == src && !files_.empty()) {
    if (showToast) {
      showToast_(String("Quelle: ") + sourceLabel_(), kToastShortMs);
    }
    if (renderNow) {
      showCurrent_();
    }
    return true;
  }

  source_ = src;
  if (!rebuildFileList_()) {
    showToast_(String("Keine Bilder: ") + sourceLabel_(), kToastLongMs);
    tft.fillScreen(TFT_BLACK);
    return false;
  }

  idx_ = 0;
  timeSinceSwitch_ = 0;
  if (renderNow) {
    showCurrent_();
  }
  if (showToast) {
    showToast_(String("Quelle: ") + sourceLabel_(), kToastShortMs);
  }
  return true;
}

String SlideshowApp::sourceLabel() const {
  return sourceLabel_();
}





bool SlideshowApp::ensureFlashReady_() {
  if (!mountLittleFs(false)) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] LittleFS mount retry");
    #endif
    if (!mountLittleFs(true)) {
      #ifdef USB_DEBUG
        Serial.println("[Slideshow] LittleFS format+mount failed");
      #endif
      return false;
    }
  }
  if (!ensureFlashSlidesDir()) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] ensureFlashSlidesDir failed");
    #endif
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
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] SD init retry @2MHz");
    #endif
    ok = SD.begin(SD_CS_PIN, sdSPI, 2000000);
  }
  if (!ok) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] SD init failed");
    #endif
  }
  return ok;
}






void SlideshowApp::enterDeleteMenu_() {
  setControlMode_(ControlMode::DeleteMenu);
}

void SlideshowApp::exitDeleteMenu_() {
  if (controlMode_ != ControlMode::DeleteMenu) return;
  deleteState_ = DeleteState::Idle;
  setControlMode_(ControlMode::Auto);
}

void SlideshowApp::requestDeleteAll_() {
  deleteState_ = DeleteState::DeleteAllConfirm;
  deleteConfirmSelection_ = 0; // Nein vorauswählen
  toastText_.clear();
  toastUntil_ = 0;
  toastDirty_ = false;
  markDeleteConfirmDirty_();
}

void SlideshowApp::confirmDeleteAll_() {
  if (deleteState_ != DeleteState::DeleteAllConfirm) return;
  if (deleteConfirmSelection_ != 1) {
    cancelDeleteAll_();
    return;
  }
  performDeleteAll_();
}

void SlideshowApp::cancelDeleteAll_() {
  deleteState_ = DeleteState::Idle;
  showToast_("Abgebrochen", kToastShortMs);
  markDeleteMenuDirty_();
}

void SlideshowApp::startDeleteSingle_() {
  if (!ensureFlashReady_()) {
    showToast_("Flash-Fehler", kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    return;
  }

  if (!rebuildFileListFrom_(SlideSource::Flash)) {
    showToast_("Keine Flash-Bilder", kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    return;
  }

  source_ = SlideSource::Flash;
  idx_ = 0;
  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  showCurrent_();
}

void SlideshowApp::requestDeleteSingle_() {
  if (deleteState_ != DeleteState::DeleteSingle) return;
  if (files_.empty()) return;

  deleteState_ = DeleteState::DeleteSingleConfirm;
  deleteCurrentFile_ = files_[idx_];
  deleteConfirmSelection_ = 0; // Nein vorauswählen
  markDeleteConfirmDirty_();
}

void SlideshowApp::confirmDeleteSingle_() {
  if (deleteState_ != DeleteState::DeleteSingleConfirm) return;
  if (deleteConfirmSelection_ != 1) {
    cancelDeleteSingle_();
    return;
  }

  performDeleteSingle_(deleteCurrentFile_);
  deleteCurrentFile_.clear();

  if (!rebuildFileListFrom_(SlideSource::Flash) || files_.empty()) {
    deleteState_ = DeleteState::Idle;
    setControlMode_(ControlMode::Auto);
    showToast_("Keine Bilder mehr", kToastLongMs);
    return;
  }

  if (idx_ >= files_.size()) {
    idx_ = 0;
  }

  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  showCurrent_();
  showToast_("Gelöscht", kToastShortMs);
}

void SlideshowApp::cancelDeleteSingle_() {
  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  if (!files_.empty()) {
    showCurrent_();
  }
}

void SlideshowApp::setUiLocked(bool locked) {
  if (uiLocked_ == locked) {
    return;
  }
  uiLocked_ = locked;
  if (!uiLocked_) {
    timeSinceSwitch_ = 0;
  }
}

void SlideshowApp::performDeleteAll_() {
  if (!ensureFlashReady_()) {
    deleteState_ = DeleteState::Error;
    showToast_("Flash-Fehler", kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    return;
  }

  std::vector<String> flashFiles;
  if (!readDirectoryEntries_(&LittleFS, kFlashSlidesDir, flashFiles)) {
    showToast_("Keine Bilder", kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    return;
  }

  deleteCount_ = 0;
  for (const String& path : flashFiles) {
    if (LittleFS.remove(path.c_str())) {
      deleteCount_++;
      #ifdef USB_DEBUG
        Serial.printf("[Delete] Removed: %s\n", path.c_str());
      #endif
    } else {
      #ifdef USB_DEBUG
        Serial.printf("[Delete] FAILED to remove: %s\n", path.c_str());
      #endif
    }
  }

  deleteState_ = DeleteState::Done;
  String msg = String(deleteCount_) + String(" Bilder gelöscht");
  showToast_(msg, kToastLongMs);

  files_.clear();
  idx_ = 0;
  deleteState_ = DeleteState::Idle;
  setControlMode_(ControlMode::Auto);

  if (!rebuildFileList_()) {
    tft.fillScreen(TFT_BLACK);
    showToast_("Keine Bilder", kToastLongMs);
  }
}

void SlideshowApp::performDeleteSingle_(const String& path) {
  if (LittleFS.remove(path.c_str())) {
    #ifdef USB_DEBUG
      Serial.printf("[Delete] Removed: %s\n", path.c_str());
    #endif
  } else {
    #ifdef USB_DEBUG
      Serial.printf("[Delete] FAILED to remove: %s\n", path.c_str());
    #endif
  }
}

void SlideshowApp::markDeleteMenuDirty_() {
  deleteMenuDirty_ = true;
}

void SlideshowApp::markDeleteConfirmDirty_() {
  deleteConfirmDirty_ = true;
}

void SlideshowApp::showCurrent_(bool allowManualOverlay, bool clearScreen) {
  if (files_.empty()) return;

  const String& path = files_[idx_];
  fs::FS* fs = filesystemFor(source_);
  if (!fs) return;

  if (source_ == SlideSource::Flash) {
    digitalWrite(SD_CS_PIN, HIGH);
  }

  File test = fs->open(path, FILE_READ);
  if (!test) {
    #ifdef USB_DEBUG
      Serial.printf("[Slideshow] open FAIL: %s\n", path.c_str());
    #endif
    return;
  }
  uint8_t sig[2] = {0, 0};
  size_t n = test.read(sig, 2);
  test.close();
  if (n != 2 || sig[0] != 0xFF || sig[1] != 0xD8) {
    #ifdef USB_DEBUG
      Serial.printf("[Slideshow] WARN SOI: %s\n", path.c_str());
    #endif
    return;
  }

  if (clearScreen) {
    tft.fillScreen(TFT_BLACK);
  }

  JRESULT rc = JDR_OK;
  if (source_ == SlideSource::SDCard) {
    rc = TJpgDec.drawSdJpg(0, 0, path.c_str());
  } else {
    digitalWrite(SD_CS_PIN, HIGH);
    rc = TJpgDec.drawFsJpg(0, 0, path.c_str(), LittleFS);
  }
  if (rc != JDR_OK) {
    #ifdef USB_DEBUG
      Serial.printf("[Slideshow] draw fail (%d): %s\n", rc, path.c_str());
    #endif
    return;
  }

  String base = path;
  int sidx = base.lastIndexOf('/');
  if (sidx >= 0) base = base.substring(sidx + 1);
  String displayName = base;
  String lower = base;
  lower.toLowerCase();
  if (lower.endsWith(".jpg")) {
    displayName = base.substring(0, base.length() - 4);
  } else if (lower.endsWith(".jpeg")) {
    displayName = base.substring(0, base.length() - 5);
  }

  if (controlMode_ == ControlMode::Manual) {
    if (allowManualOverlay) {
      manualFilenameLabel_ = displayName;
      manualFilenameActive_ = true;
      manualFilenameDirty_ = true;
      if (show_filename) {
        manualFilenameUntil_ = 0;
      } else {
        manualFilenameUntil_ = millis() + kManualFilenameDurationMs;
      }
    } else if (!show_filename) {
      manualFilenameActive_ = false;
      manualFilenameLabel_.clear();
      manualFilenameUntil_ = 0;
      manualFilenameDirty_ = true;
    }
  } else {
    if (manualFilenameActive_ || !manualFilenameLabel_.isEmpty()) {
      manualFilenameDirty_ = true;
    }
    manualFilenameActive_ = false;
    manualFilenameLabel_.clear();
    manualFilenameUntil_ = 0;
    if (show_filename) {
      int16_t y = TFT_H - TextRenderer::lineHeight() - 4;
      if (y < 0) y = 0;
      TextRenderer::drawCentered(y, displayName, TFT_WHITE, TFT_BLACK);
    }
  }

  if (toastUntil_) {
    toastDirty_ = true;
  }
  drawToastOverlay_();

  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] OK (%s): %s\n", slideSourceLabel(source_), path.c_str());
  #endif

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
    case ControlMode::DeleteMenu:
      return String("LÖSCHEN");
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
  if (controlMode_ == ControlMode::DeleteMenu) {
    markDeleteMenuDirty_();
    return;
  }
  drawToastOverlay_();
}

void SlideshowApp::drawManualFilenameOverlay_() {
  const bool inManualMode = (controlMode_ == ControlMode::Manual);
  if (!inManualMode || manualFilenameLabel_.isEmpty() || !manualFilenameActive_) {
    manualFilenameDirty_ = false;
    return;
  }

  if (!show_filename) {
    if (!manualFilenameUntil_) {
      manualFilenameActive_ = false;
      manualFilenameLabel_.clear();
      manualFilenameDirty_ = false;
      return;
    }
    const uint32_t now = millis();
    if (now >= manualFilenameUntil_) {
      manualFilenameActive_ = false;
      manualFilenameLabel_.clear();
      manualFilenameUntil_ = 0;
      manualFilenameDirty_ = false;
      showCurrent_(false, false);
      return;
    }
  }

  if (!manualFilenameDirty_) {
    return;
  }
  manualFilenameDirty_ = false;

  const int16_t lineHeight = TextRenderer::lineHeight();
  int16_t yTop = (TFT_H - lineHeight) / 2;
  if (yTop < 0) yTop = 0;

  TextRenderer::drawCentered(yTop, manualFilenameLabel_, TFT_WHITE, TFT_BLACK);
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






void SlideshowApp::drawDeleteMenuOverlay_() {
  if (!deleteMenuDirty_) return;
  deleteMenuDirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  const int16_t top = 28;
  const int16_t option1Y = top + line + spacing;
  const int16_t option2Y = option1Y + line + spacing;
  const int16_t hint1Y = option2Y + line + spacing + 8;
  const int16_t hint2Y = hint1Y + line + spacing;

  TextRenderer::drawCentered(top, "Lösch-Menü", TFT_WHITE, TFT_BLACK);

  String option1 = (deleteMenuSelection_ == 0) ? String("> Alle löschen") : String("Alle löschen");
  String option2 = (deleteMenuSelection_ == 1) ? String("> Einzeln") : String("Einzeln");

  TextRenderer::drawCentered(option1Y, option1, TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(option2Y, option2, TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(hint1Y, "Doppel: Starten", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(hint2Y, "Lang: Auto", TFT_WHITE, TFT_BLACK);
}

void SlideshowApp::drawDeleteAllConfirmOverlay_() {
  if (!deleteConfirmDirty_) return;
  deleteConfirmDirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 32;
  TextRenderer::drawCentered(top, "Flash", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line, "löschen?", TFT_WHITE, TFT_BLACK);

  String options = (deleteConfirmSelection_ == 0) ? String("> Nein    Ja")
                                                  : String("Nein    > Ja");
  TextRenderer::drawCentered(top + line * 2 + 16, options, TFT_WHITE, TFT_BLACK);

  TextRenderer::drawCentered(top + line * 3 + 28, "Klick: Wechseln", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line * 4 + 28, "Lang: Bestätigen", TFT_WHITE, TFT_BLACK);
}

void SlideshowApp::drawDeleteSingleConfirmOverlay_() {
  if (!deleteConfirmDirty_) return;
  deleteConfirmDirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 26;

  TextRenderer::drawCentered(top, "Bild löschen?", TFT_WHITE, TFT_BLACK);

  String filename = deleteCurrentFile_;
  int s = filename.lastIndexOf('/');
  if (s >= 0) filename = filename.substring(s + 1);

  TextRenderer::drawCentered(top + line + 6, filename, TFT_WHITE, TFT_BLACK);

  String options = (deleteConfirmSelection_ == 0) ? String("> Nein    Ja")
                                                  : String("Nein    > Ja");
  TextRenderer::drawCentered(top + line * 2 + 20, options, TFT_WHITE, TFT_BLACK);

  TextRenderer::drawCentered(top + line * 3 + 32, "Klick: Wechseln", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line * 4 + 32, "Lang: Bestätigen", TFT_WHITE, TFT_BLACK);
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
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] rebuild: SD not ready");
    #endif
  }
  if (rebuildFileListFrom_(source_)) {
    idx_ = 0;
    return true;
  }

  if (source_ == SlideSource::SDCard) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] SD leer – auf Flash umschalten");
    #endif
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
  deleteState_ = DeleteState::Idle;
  deleteMenuSelection_ = 0;
  deleteConfirmSelection_ = 0;
  deleteMenuDirty_ = true;
  deleteConfirmDirty_ = true;
  deleteSingleTimer_ = 0;
  deleteCurrentFile_.clear();
  deleteCount_ = 0;

  applyDwell_();

  if (!rebuildFileList_()) {
    tft.fillScreen(TFT_BLACK);
    const int16_t line = TextRenderer::lineHeight();
    const int16_t centerY = (TFT_H - line * 2) / 2;
    TextRenderer::drawCentered(centerY, "Keine Bilder", TFT_WHITE, TFT_BLACK);
    TextRenderer::drawCentered(centerY + line, "gefunden", TFT_WHITE, TFT_BLACK);
    return;
  }

  showCurrent_();
  timeSinceSwitch_ = 0;
  showToast_(modeLabel_(), kToastShortMs);
}

void SlideshowApp::tick(uint32_t delta_ms) {
  const uint32_t now = millis();

  if (uiLocked_) {
    return;
  }

  if (toastUntil_ && now >= toastUntil_) {
    toastUntil_ = 0;
    toastText_.clear();
    toastDirty_ = false;
    if (controlMode_ == ControlMode::DeleteMenu) {
      markDeleteMenuDirty_();
    } else if (!files_.empty()) {
      showCurrent_();
    }
  }

  if (deleteState_ == DeleteState::DeleteSingle) {
    deleteSingleTimer_ += delta_ms;
    if (deleteSingleTimer_ >= 2000) {
      deleteSingleTimer_ = 0;
      advance_(+1);
    }
    return;
  }

  if (files_.empty()) {
    static uint32_t lastCheckTime = 0;
    const uint32_t now = millis();
    if (now - lastCheckTime >= 1000) {
      lastCheckTime = now;
      if (rebuildFileList_()) {
        showCurrent_();
        if (controlMode_ == ControlMode::Auto) {
          showToast_(modeLabel_(), kToastShortMs);
        }
      }
    }
    return;
  }

  if (!auto_mode) return;

  timeSinceSwitch_ += delta_ms;
  if (timeSinceSwitch_ >= dwell_ms) {
    advance_(+1);
  }
}

void SlideshowApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (deleteState_) {
    case DeleteState::DeleteAllConfirm:
      if (e == BtnEvent::Single) {
        deleteConfirmSelection_ ^= 1;
        markDeleteConfirmDirty_();
      } else if (e == BtnEvent::Long) {
        confirmDeleteAll_();
      }
      return;

    case DeleteState::DeleteSingle:
      if (e == BtnEvent::Single) {
        requestDeleteSingle_();
      } else if (e == BtnEvent::Long) {
        deleteState_ = DeleteState::Idle;
        markDeleteMenuDirty_();
      }
      return;

    case DeleteState::DeleteSingleConfirm:
      if (e == BtnEvent::Single) {
        deleteConfirmSelection_ ^= 1;
        markDeleteConfirmDirty_();
      } else if (e == BtnEvent::Long) {
        confirmDeleteSingle_();
      }
      return;

    default:
      break;
  }

  switch (e) {
    case BtnEvent::Single:
      if (controlMode_ == ControlMode::DeleteMenu) {
        deleteMenuSelection_ ^= 1;
        markDeleteMenuDirty_();
      } else {
        advance_(+1);
      }
      break;

    case BtnEvent::Double:
      if (controlMode_ == ControlMode::DeleteMenu) {
        if (deleteMenuSelection_ == 0) {
          requestDeleteAll_();
        } else {
          startDeleteSingle_();
        }
      } else {
        dwellIdx_ = (dwellIdx_ + 1) % kDwellSteps.size();
        applyDwell_();
        timeSinceSwitch_ = 0;
        showToast_(dwellToastLabel_(), kToastShortMs);
        #ifdef USB_DEBUG
          Serial.printf("[Slideshow] dwell=%lu ms\n", (unsigned long)dwell_ms);
        #endif
      }
      break;

    case BtnEvent::Triple:
      if (controlMode_ == ControlMode::DeleteMenu) {
        showToast_("Lang: Auto", kToastShortMs);
      } else {
        show_filename = !show_filename;
        if (controlMode_ == ControlMode::Manual) {
          if (show_filename) {
            showCurrent_();
          } else {
            manualFilenameActive_ = false;
            manualFilenameLabel_.clear();
            manualFilenameUntil_ = 0;
            showCurrent_(false, false);
          }
        } else {
          showCurrent_();
        }
      }
      break;

    case BtnEvent::Long: {
      if (controlMode_ == ControlMode::Auto) {
        if (files_.empty()) {
          showToast_("Keine Bilder", kToastShortMs);
        } else {
          setControlMode_(ControlMode::Manual);
        }
      } else if (controlMode_ == ControlMode::Manual) {
        setControlMode_(ControlMode::DeleteMenu);
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
  if (deleteState_ == DeleteState::DeleteAllConfirm) {
    drawDeleteAllConfirmOverlay_();
    return;
  }
  if (deleteState_ == DeleteState::DeleteSingleConfirm) {
    drawDeleteSingleConfirmOverlay_();
    return;
  }
  if (controlMode_ == ControlMode::DeleteMenu && deleteState_ == DeleteState::Idle) {
    drawDeleteMenuOverlay_();
    return;
  }
  drawManualFilenameOverlay_();
  drawToastOverlay_();
}

void SlideshowApp::shutdown() {
  files_.clear();
}
