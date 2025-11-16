#include "SlideshowApp.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Storage.h"
#include "Core/TextRenderer.h"
#include "Core/I18n.h"

namespace {
constexpr std::array<uint32_t, 6> kDwellSteps{1000, 5000, 10000, 30000, 300000, 600000};
constexpr uint32_t kToastShortMs = 1000;
constexpr uint32_t kToastLongMs = 1500;
constexpr uint32_t kManualFilenameDurationMs = 2000;
constexpr uint32_t kDeleteHintDurationMs = 6000;
}

bool SlideshowApp::isJpeg_(const String& n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

bool SlideshowApp::isGif_(const String& n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".gif");
}

bool SlideshowApp::isMediaFile_(const String& n) {
  return isJpeg_(n) || isGif_(n);
}

bool SlideshowApp::focusTransferredFile(const char* filename, size_t size) {
  if (!ensureFlashReady_()) {
    showToast_(i18n.t("errors.flash_error"), 1800);
    return false;
  }
  if (!rebuildFileListFrom_(SlideSource::Flash)) {
    showToast_(i18n.t("errors.no_flash_images"), 1500);
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
    String msg = i18n.t("slideshow.transfer_done", filename);
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

  if (mode != ControlMode::Manual) {
    closeMenus_();
    clearHelperOverlay_();
  }

  controlMode_ = mode;
  if (controlMode_ != ControlMode::Manual) {
    manualFilenameActive_ = false;
    manualFilenameLabel_.clear();
    manualFilenameUntil_ = 0;
  }

  switch (controlMode_) {
    case ControlMode::Auto:
      auto_mode = true;
      // When switching to Auto mode from menu, redraw current image immediately
      if (previous != ControlMode::Auto && !files_.empty()) {
        showCurrent_(true, true);
      }
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
      TextRenderer::drawCentered(centerY, i18n.t("slideshow.no_images"), TFT_WHITE, TFT_BLACK);
      TextRenderer::drawCentered(centerY + line, i18n.t("slideshow.found"), TFT_WHITE, TFT_BLACK);
    }
  }
}

void SlideshowApp::setSource_(SlideSource src, bool showToast) {
  if (source_ == src && !files_.empty()) {
    if (showToast) showToast_(i18n.t("slideshow.source_label", sourceLabel_()), kToastShortMs);
    return;
  }

  source_ = src;
  if (!rebuildFileList_()) {
    showToast_(i18n.t("slideshow.no_images_source", sourceLabel_()), kToastLongMs);
    tft.fillScreen(TFT_BLACK);
    return;
  }

  idx_ = 0;
  timeSinceSwitch_ = 0;
  showCurrent_();
  if (showToast) {
    showToast_(i18n.t("slideshow.source_label", sourceLabel_()), kToastShortMs);
  }
}

bool SlideshowApp::setSlideSource(SlideSource src, bool showToast, bool renderNow) {
  if (source_ == src && !files_.empty()) {
    if (showToast) {
      showToast_(i18n.t("slideshow.source_label", sourceLabel_()), kToastShortMs);
    }
    if (renderNow) {
      showCurrent_();
    }
    return true;
  }

  source_ = src;
  if (!rebuildFileList_()) {
    showToast_(i18n.t("slideshow.no_images_source", sourceLabel_()), kToastLongMs);
    tft.fillScreen(TFT_BLACK);
    return false;
  }

  idx_ = 0;
  timeSinceSwitch_ = 0;
  if (renderNow) {
    showCurrent_();
  }
  if (showToast) {
    showToast_(i18n.t("slideshow.source_label", sourceLabel_()), kToastShortMs);
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

  // Avoid frequent SD.begin() calls as they block for several seconds
  // Only try once per 30 seconds when no card is present
  static uint32_t lastSdInitAttempt = 0;
  const uint32_t now = millis();
  if (now - lastSdInitAttempt < 30000) {
    return false;  // Too soon, don't try again
  }
  lastSdInitAttempt = now;

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

void SlideshowApp::openSlideshowMenu_() {
  menuScreen_ = MenuScreen::Slideshow;
  if (slideshowMenuSelection_ >= 4) slideshowMenuSelection_ = 0;
  slideshowMenuDirty_ = true;
  sourceMenuDirty_ = false;
  autoSpeedMenuDirty_ = false;
}

void SlideshowApp::closeMenus_() {
  if (menuScreen_ == MenuScreen::None) {
    return;
  }
  menuScreen_ = MenuScreen::None;
  slideshowMenuDirty_ = false;
  sourceMenuDirty_ = false;
  autoSpeedMenuDirty_ = false;
}

void SlideshowApp::openSourceMenu_() {
  menuScreen_ = MenuScreen::Source;
  sourceMenuSelection_ = (source_ == SlideSource::Flash) ? 1 : 0;
  sourceMenuDirty_ = true;
  autoSpeedMenuDirty_ = false;
}

void SlideshowApp::openAutoSpeedMenu_() {
  menuScreen_ = MenuScreen::AutoSpeed;
  if (dwellIdx_ >= kDwellSteps.size()) {
    dwellIdx_ = 0;
  }
  autoSpeedSelection_ = dwellIdx_;
  autoSpeedMenuDirty_ = true;
  sourceMenuDirty_ = false;
}

void SlideshowApp::handleMenuButton_(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single:
      slideshowMenuSelection_ = (slideshowMenuSelection_ + 1) % 4;
      slideshowMenuDirty_ = true;
      break;
    case BtnEvent::Long:
      switch (slideshowMenuSelection_) {
        case 0:
          openSourceMenu_();
          break;
        case 1:
          closeMenus_();
          setControlMode_(ControlMode::DeleteMenu, false);
          break;
        case 2:
          openAutoSpeedMenu_();
          break;
        case 3:
        default:
          closeMenus_();
          setControlMode_(ControlMode::Auto);
          break;
      }
      break;
    case BtnEvent::Double:
      closeMenus_();
      break;
    default:
      break;
  }
}

void SlideshowApp::handleSourceMenuButton_(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single:
      sourceMenuSelection_ = (sourceMenuSelection_ + 1) % 3;
      sourceMenuDirty_ = true;
      break;
    case BtnEvent::Long:
      if (sourceMenuSelection_ == 2) {
        openSlideshowMenu_();
        return;
      }
      {
        SlideSource target = (sourceMenuSelection_ == 0) ? SlideSource::SDCard
                                                        : SlideSource::Flash;
        closeMenus_();
        setSlideSource(target, true, true);
      }
      break;
    case BtnEvent::Double:
      openSlideshowMenu_();
      break;
    default:
      break;
  }
}

void SlideshowApp::handleAutoSpeedMenuButton_(BtnEvent e) {
  const uint8_t optionCount = static_cast<uint8_t>(kDwellSteps.size());
  switch (e) {
    case BtnEvent::Single:
      autoSpeedSelection_ = (autoSpeedSelection_ + 1) % (optionCount + 1);
      autoSpeedMenuDirty_ = true;
      break;
    case BtnEvent::Long:
      if (autoSpeedSelection_ == optionCount) {
        openSlideshowMenu_();
      } else {
        dwellIdx_ = autoSpeedSelection_;
        applyDwell_();
        timeSinceSwitch_ = 0;
        closeMenus_();
        showToast_(dwellToastLabel_(), kToastShortMs);
        setControlMode_(ControlMode::Auto);
      }
      break;
    case BtnEvent::Double:
      openSlideshowMenu_();
      break;
    default:
      break;
  }
}

void SlideshowApp::drawSlideshowMenu_() {
  if (!slideshowMenuDirty_) return;
  slideshowMenuDirty_ = false;

  tft.fillScreen(TFT_BLACK);
  const char* itemKeys[4] = {"slideshow.source_select", "slideshow.delete_menu", "slideshow.auto_speed", "menu.exit"};
  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 12;
  const int16_t top = 24;
  TextRenderer::drawCentered(top, i18n.t("slideshow.menu_title"), TFT_WHITE, TFT_BLACK);

  for (uint8_t i = 0; i < 4; ++i) {
    int16_t y = top + line + spacing + 15 + static_cast<int16_t>(i) * (line + spacing);
    char buf[64];
    const char* label = i18n.t(itemKeys[i]);
    if (slideshowMenuSelection_ == i) {
      snprintf(buf, sizeof(buf), "> %s", label);
    } else {
      snprintf(buf, sizeof(buf), "%s", label);
    }
    uint16_t color = (slideshowMenuSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    TextRenderer::drawCentered(y, buf, color, TFT_BLACK);
  }

  const int16_t helperBase = TFT_H - TextRenderer::helperLineHeight() - 24;
  TextRenderer::drawHelperCentered(helperBase - (TextRenderer::helperLineHeight() + 4),
                                   i18n.t("buttons.short_switch"),
                                   TFT_WHITE,
                                   TFT_BLACK);
  TextRenderer::drawHelperCentered(helperBase,
                                   i18n.t("buttons.long_open"),
                                   TFT_WHITE,
                                   TFT_BLACK);
}

void SlideshowApp::drawSourceMenu_() {
  if (!sourceMenuDirty_) return;
  sourceMenuDirty_ = false;

  tft.fillScreen(TFT_BLACK);
  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 12;
  const int16_t top = 22;
  TextRenderer::drawCentered(top, i18n.t("slideshow.source_select"), TFT_WHITE, TFT_BLACK);

  const char* labelKeys[3] = {"slideshow.sd_card", "slideshow.flash", "slideshow.back"};
  for (uint8_t i = 0; i < 3; ++i) {
    char buf[64];
    const char* label = i18n.t(labelKeys[i]);
    bool isCurrent = false;
    if (i < 2) {
      SlideSource current = source_;
      isCurrent = (i == 0 && current == SlideSource::SDCard) ||
                  (i == 1 && current == SlideSource::Flash);
    }
    if (sourceMenuSelection_ == i) {
      snprintf(buf, sizeof(buf), "> %s%s", label, isCurrent ? " *" : "");
    } else {
      snprintf(buf, sizeof(buf), "%s%s", label, isCurrent ? " *" : "");
    }
    uint16_t color = (sourceMenuSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    int16_t y = top + line + spacing + 15 + static_cast<int16_t>(i) * (line + spacing);
    TextRenderer::drawCentered(y, buf, color, TFT_BLACK);
  }

  const int16_t helperY = TFT_H - (TextRenderer::helperLineHeight() * 2) - 37;
  TextRenderer::drawHelperCentered(helperY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY + TextRenderer::helperLineHeight() + 2,
                                   i18n.t("buttons.long_select"),
                                   TFT_WHITE,
                                   TFT_BLACK);
}

void SlideshowApp::drawAutoSpeedMenu_() {
  if (!autoSpeedMenuDirty_) return;
  autoSpeedMenuDirty_ = false;

  tft.fillScreen(TFT_BLACK);
  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  int16_t top = 17;
  TextRenderer::drawCentered(top, i18n.t("slideshow.auto"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line + 4, i18n.t("slideshow.speed"), TFT_WHITE, TFT_BLACK);

  const uint8_t optionCount = static_cast<uint8_t>(kDwellSteps.size());
  const uint8_t leftCount = (optionCount + 1) / 2;
  const uint8_t rows = leftCount;
  const int16_t columnCenters[2] = {TFT_W / 4, (TFT_W * 3) / 4};

  for (uint8_t i = 0; i < optionCount; ++i) {
    bool leftColumn = (i < leftCount);
    uint8_t row = leftColumn ? i : (i - leftCount);
    int16_t y = top + line*2 + 27 + static_cast<int16_t>(row) * (line + spacing);
    String text = dwellOptionLabel_(i);
    if (autoSpeedSelection_ == i) {
      text = String("> ") + text;
    }
    uint16_t color = (autoSpeedSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    int16_t width = TextRenderer::measure(text);
    int16_t x = columnCenters[leftColumn ? 0 : 1] - (width / 2);
    TextRenderer::draw(x, y, text, color, TFT_BLACK);
  }

  int16_t exitY = top + line*2 + 27 + static_cast<int16_t>(rows) * (line + spacing);
  String exitLabel = (autoSpeedSelection_ == optionCount) ? String("> ") + String(i18n.t("slideshow.back")) : String(i18n.t("slideshow.back"));
  uint16_t exitColor = (autoSpeedSelection_ == optionCount) ? TFT_WHITE : TFT_DARKGREY;
  TextRenderer::drawCentered(exitY, exitLabel, exitColor, TFT_BLACK);

  const int16_t helperY = TFT_H - (TextRenderer::helperLineHeight() * 2) - 30;
  TextRenderer::drawHelperCentered(helperY,
                                   i18n.t("buttons.short_switch"),
                                   TFT_WHITE,
                                   TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY + TextRenderer::helperLineHeight() + 2,
                                   i18n.t("buttons.long_set"),
                                   TFT_WHITE,
                                   TFT_BLACK);
}

String SlideshowApp::dwellOptionLabel_(uint8_t idx) const {
  static char buf[16];
  if (idx >= kDwellSteps.size()) {
    return String();
  }
  uint32_t ms = kDwellSteps[idx];
  if (ms % 60000 == 0) {
    uint32_t minutes = ms / 60000;
    snprintf(buf, sizeof(buf), "%lu min", minutes);
  } else {
    uint32_t seconds = (ms + 500) / 1000;
    snprintf(buf, sizeof(buf), "%lu s", seconds);
  }
  return buf;
}






void SlideshowApp::enterDeleteMenu_() {
  deleteMenuSelection_ = 0;
  markDeleteMenuDirty_();
  clearHelperOverlay_();
  setControlMode_(ControlMode::DeleteMenu);
}

void SlideshowApp::exitDeleteMenu_() {
  if (controlMode_ != ControlMode::DeleteMenu) return;
  deleteState_ = DeleteState::Idle;
  clearHelperOverlay_();
  setControlMode_(ControlMode::Auto);
}

void SlideshowApp::requestDeleteAll_() {
  deleteState_ = DeleteState::DeleteAllConfirm;
  deleteConfirmSelection_ = 0; // Nein vorauswählen
  toastText_.clear();
  toastUntil_ = 0;
  toastDirty_ = false;
  helperLinePrimary_.clear();
  helperLineSecondary_.clear();
  helperLinesUntil_ = 0;
  helperLinesDirty_ = true;
  markDeleteConfirmDirty_();
  showCurrent_(false, true);
  clearHelperOverlay_();
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
  showToast_(i18n.t("slideshow.aborted"), kToastShortMs);
  markDeleteMenuDirty_();
  clearHelperOverlay_();
}

void SlideshowApp::startDeleteSingle_() {
  if (!ensureFlashReady_()) {
    showToast_(i18n.t("errors.flash_error"), kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    clearHelperOverlay_();
    return;
  }

  if (!rebuildFileListFrom_(SlideSource::Flash)) {
    showToast_(i18n.t("errors.no_flash_images"), kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    return;
  }

  source_ = SlideSource::Flash;
  idx_ = 0;
  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  showCurrent_();
  showHelperOverlay_(
      i18n.t("buttons.long_delete"),
      i18n.t("buttons.short_next"),
      i18n.t("buttons.double_exit"),
      kDeleteHintDurationMs);
}

void SlideshowApp::requestDeleteSingle_() {
  if (deleteState_ != DeleteState::DeleteSingle) return;
  if (files_.empty()) return;

  deleteState_ = DeleteState::DeleteSingleConfirm;
  deleteCurrentFile_ = files_[idx_];
  deleteConfirmSelection_ = 0; // Nein vorauswählen
  markDeleteConfirmDirty_();
  showCurrent_(false, false);
  clearHelperOverlay_();
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
    clearHelperOverlay_();
    showToast_(i18n.t("slideshow.no_more_images"), kToastLongMs);
    return;
  }

  if (idx_ >= files_.size()) {
    idx_ = 0;
  }

  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  showCurrent_();
  showHelperOverlay_(
      i18n.t("buttons.long_delete"),
      i18n.t("buttons.short_next"),
      i18n.t("buttons.double_exit"),
      kDeleteHintDurationMs);
  showToast_(i18n.t("slideshow.deleted"), kToastShortMs);
}

void SlideshowApp::cancelDeleteSingle_() {
  deleteState_ = DeleteState::DeleteSingle;
  deleteSingleTimer_ = 0;
  if (!files_.empty()) {
    showCurrent_();
    showHelperOverlay_(
        "BTN2 lang: Löschen",
        "BTN2 kurz: Nächstes Bild",
        "BTN2 doppelt: Exit",
        kDeleteHintDurationMs);
  } else {
    clearHelperOverlay_();
  }
}

void SlideshowApp::setUiLocked(bool locked) {
  if (uiLocked_ == locked) {
    return;
  }
  uiLocked_ = locked;
  if (uiLocked_) {
    clearHelperOverlay_();
    closeMenus_();
  } else {
    timeSinceSwitch_ = 0;
  }
}

void SlideshowApp::performDeleteAll_() {
  if (!ensureFlashReady_()) {
    deleteState_ = DeleteState::Error;
    showToast_(i18n.t("errors.flash_error"), kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    clearHelperOverlay_();
    return;
  }

  std::vector<String> flashFiles;
  if (!readDirectoryEntries_(&LittleFS, kFlashSlidesDir, flashFiles)) {
    showToast_(i18n.t("slideshow.no_images"), kToastLongMs);
    deleteState_ = DeleteState::Idle;
    markDeleteMenuDirty_();
    clearHelperOverlay_();
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
  String msg = i18n.t("slideshow.images_deleted", String(deleteCount_));
  showToast_(msg, kToastLongMs);

  files_.clear();
  idx_ = 0;
  deleteState_ = DeleteState::Idle;
  clearHelperOverlay_();
  setControlMode_(ControlMode::Auto);

  if (!rebuildFileList_()) {
    tft.fillScreen(TFT_BLACK);
    showToast_(i18n.t("slideshow.no_images"), kToastLongMs);
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

void SlideshowApp::returnToSlideshowMenu_() {
  deleteState_ = DeleteState::Idle;
  clearHelperOverlay_();
  setControlMode_(ControlMode::Manual, false);
  openSlideshowMenu_();
}

void SlideshowApp::showCurrent_(bool allowManualOverlay, bool clearScreen) {
  if (files_.empty()) return;

  const String& path = files_[idx_];

  // Check if this is a GIF file
  if (isGif_(path)) {
    showCurrentGif_(allowManualOverlay, clearScreen);
    return;
  }

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

  // Stop any playing GIF when advancing
  stopGif_();

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
  autoSpeedSelection_ = dwellIdx_;
}

String SlideshowApp::dwellLabel_() const {
  static char buf[12];
  if (dwell_ms % 60000 == 0) {
    snprintf(buf, sizeof(buf), "%lum", dwell_ms / 60000);
  } else {
    snprintf(buf, sizeof(buf), "%lus", (dwell_ms + 500) / 1000);
  }
  return buf;
}

String SlideshowApp::modeLabel_() const {
  switch (controlMode_) {
    case ControlMode::Auto:
      return i18n.t("slideshow.mode_auto", dwellLabel_());
    case ControlMode::Manual:
      return i18n.t("slideshow.mode_manual");
    case ControlMode::DeleteMenu:
      return i18n.t("slideshow.mode_delete");
  }
  return String("?");
}

String SlideshowApp::sourceLabel_() const {
  return String(slideSourceLabel(source_));
}

String SlideshowApp::dwellToastLabel_() const {
  return i18n.t("slideshow.duration", dwellLabel_());
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

  if (controlMode_ == ControlMode::Manual && toastUntil_) {
    manualFilenameDirty_ = true;
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

void SlideshowApp::showHelperOverlay_(const String& primary,
                                      const String& secondary,
                                      const String& tertiary,
                                      uint32_t duration_ms) {
  helperLinePrimary_ = primary;
  helperLineSecondary_ = secondary;
  helperLineTertiary_ = tertiary;
  helperLinesUntil_ = millis() + duration_ms;
  helperLinesDirty_ = true;
}

void SlideshowApp::clearHelperOverlay_() {
  if (helperLinePrimary_.isEmpty() && helperLineSecondary_.isEmpty() && helperLineTertiary_.isEmpty()) {
    helperLinesUntil_ = 0;
    return;
  }
  helperLinePrimary_.clear();
  helperLineSecondary_.clear();
  helperLineTertiary_.clear();
  helperLinesUntil_ = 0;
  helperLinesDirty_ = true;
}

void SlideshowApp::drawHelperOverlay_() {
  const int16_t lineHeight = TextRenderer::helperLineHeight();
  const int16_t yThird = TFT_H - 20 - lineHeight;
  const int16_t ySecond = yThird - (lineHeight + 5);
  const int16_t yFirst = ySecond - (lineHeight + 5);
  const int16_t blockTop = max<int16_t>(0, yFirst - 6);
  const int16_t blockBottom = min<int16_t>(TFT_H, yThird + lineHeight + 6);

  const bool hasLines =
      !(helperLinePrimary_.isEmpty() && helperLineSecondary_.isEmpty() && helperLineTertiary_.isEmpty());
  if (!hasLines) {
    if (helperLinesDirty_) {
      helperLinesDirty_ = false;
      showCurrent_(false, false);
    }
    return;
  }

  if (helperLinesUntil_ && millis() >= helperLinesUntil_) {
    helperLinePrimary_.clear();
    helperLineSecondary_.clear();
    helperLineTertiary_.clear();
    helperLinesUntil_ = 0;
    helperLinesDirty_ = true;
    showCurrent_(false, false);
    return;
  }

  if (!helperLinesDirty_) {
    return;
  }
  helperLinesDirty_ = false;
  tft.fillRect(0, blockTop, TFT_W, blockBottom - blockTop, TFT_BLACK);
  if (!helperLinePrimary_.isEmpty()) {
    TextRenderer::drawHelperCentered(yFirst, helperLinePrimary_, TFT_WHITE, TFT_BLACK);
  }
  if (!helperLineSecondary_.isEmpty()) {
    TextRenderer::drawHelperCentered(ySecond, helperLineSecondary_, TFT_WHITE, TFT_BLACK);
  }
  if (!helperLineTertiary_.isEmpty()) {
    TextRenderer::drawHelperCentered(yThird, helperLineTertiary_, TFT_WHITE, TFT_BLACK);
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






void SlideshowApp::drawDeleteMenuOverlay_() {
  if (!deleteMenuDirty_) return;
  deleteMenuDirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  const int16_t top = 28;
  const int16_t helperY = TFT_H - (TextRenderer::helperLineHeight() * 2) - 28;

  TextRenderer::drawCentered(top, i18n.t("slideshow.delete_menu"), TFT_WHITE, TFT_BLACK);

  const char* labelKeys[3] = {"slideshow.all_delete", "slideshow.single_delete", "menu.exit"};
  for (uint8_t i = 0; i < 3; ++i) {
    String text = (deleteMenuSelection_ == i) ? String("> ") + String(i18n.t(labelKeys[i])) : String(i18n.t(labelKeys[i]));
    uint16_t color = (deleteMenuSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    int16_t y = top + line + spacing + 15 + static_cast<int16_t>(i) * (line + spacing);
    TextRenderer::drawCentered(y, text, color, TFT_BLACK);
  }
  TextRenderer::drawHelperCentered(helperY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY + TextRenderer::helperLineHeight() + 2,
                                   i18n.t("buttons.long_open"),
                                   TFT_WHITE,
                                   TFT_BLACK);
}

void SlideshowApp::drawDeleteAllConfirmOverlay_() {
  if (!deleteConfirmDirty_) return;
  deleteConfirmDirty_ = false;

  tft.fillScreen(TFT_BLACK);
  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 32;
  TextRenderer::drawCentered(top, i18n.t("slideshow.delete_flash"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line, i18n.t("slideshow.delete_flash_question"), TFT_WHITE, TFT_BLACK);

  const int16_t optionTop = top + line * 2 + 16;
  const char* confirmKeys[3] = {"system.no", "system.yes", "menu.exit"};
  for (uint8_t i = 0; i < 3; ++i) {
    String text = String(i18n.t(confirmKeys[i]));
    if (deleteConfirmSelection_ == i) {
      text = String("> ") + text;
    }
    uint16_t color = (deleteConfirmSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    int16_t y = optionTop + static_cast<int16_t>(i) * (line + 6);
    TextRenderer::drawCentered(y, text, color, TFT_BLACK);
  }

  const int16_t helperY = TFT_H - (TextRenderer::helperLineHeight() * 2) - 27;
  tft.fillRect(0, helperY - 8, TFT_W,
               TextRenderer::helperLineHeight() * 2 + 24, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY + TextRenderer::helperLineHeight() + 2,
                                   i18n.t("buttons.long_confirm"),
                                   TFT_WHITE,
                                   TFT_BLACK);
}

void SlideshowApp::drawDeleteSingleConfirmOverlay_() {
  if (!deleteConfirmDirty_) return;
  deleteConfirmDirty_ = false;
  showCurrent_(false, false);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 56;

  TextRenderer::drawCentered(top, i18n.t("slideshow.delete_image"), TFT_WHITE, TFT_BLACK);

  String filename = deleteCurrentFile_;
  int s = filename.lastIndexOf('/');
  if (s >= 0) filename = filename.substring(s + 1);

  TextRenderer::drawCentered(top + line + 6, filename, TFT_WHITE, TFT_BLACK);

  const int16_t singleOptionTop = top + line * 2 + 20;
  const char* singleKeys[3] = {"system.no", "system.yes", "menu.exit"};
  for (uint8_t i = 0; i < 3; ++i) {
    String text = String(i18n.t(singleKeys[i]));
    if (deleteConfirmSelection_ == i) {
      text = String("> ") + text;
    }
    uint16_t color = (deleteConfirmSelection_ == i) ? TFT_WHITE : TFT_DARKGREY;
    int16_t y = singleOptionTop + static_cast<int16_t>(i) * (line + 6);
    TextRenderer::drawCentered(y, text, color, TFT_BLACK);
  }

  const int16_t helperY = TFT_H - (TextRenderer::helperLineHeight() * 2) - 27;
  tft.fillRect(0, helperY - 8, TFT_W,
               TextRenderer::helperLineHeight() * 2 + 24, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(helperY + TextRenderer::helperLineHeight() + 2,
                                   i18n.t("buttons.long_confirm"),
                                   TFT_WHITE,
                                   TFT_BLACK);
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
    if (!isMediaFile_(base)) continue;

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
  menuScreen_ = MenuScreen::None;
  slideshowMenuSelection_ = 0;
  sourceMenuSelection_ = 0;
  autoSpeedSelection_ = 0;
  slideshowMenuDirty_ = false;
  sourceMenuDirty_ = false;
  autoSpeedMenuDirty_ = false;

  // Initialize GIF decoder
  gif_.begin(BIG_ENDIAN_PIXELS);

  applyDwell_();

  if (!rebuildFileList_()) {
    tft.fillScreen(TFT_BLACK);
    const int16_t line = TextRenderer::lineHeight();
    const int16_t centerY = (TFT_H - line * 2) / 2;
    TextRenderer::drawCentered(centerY, i18n.t("slideshow.no_images"), TFT_WHITE, TFT_BLACK);
    TextRenderer::drawCentered(centerY + line, i18n.t("slideshow.found"), TFT_WHITE, TFT_BLACK);
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
    if (deleteSingleTimer_ >= 6000) {
      deleteSingleTimer_ = 0;
      advance_(+1);
      showHelperOverlay_(
          "BTN2 lang: Löschen",
          "BTN2 kurz: Nächstes Bild",
          "BTN2 doppelt: Exit",
          kDeleteHintDurationMs);
    }
    return;
  }

  if (files_.empty()) {
    static uint32_t lastCheckTime = 0;
    const uint32_t now = millis();
    // Only check every 5 seconds to avoid blocking SD.begin() calls
    if (now - lastCheckTime >= 5000) {
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

  if (menuScreen_ == MenuScreen::Slideshow) {
    handleMenuButton_(e);
    return;
  }
  if (menuScreen_ == MenuScreen::Source) {
    handleSourceMenuButton_(e);
    return;
  }
  if (menuScreen_ == MenuScreen::AutoSpeed) {
    handleAutoSpeedMenuButton_(e);
    return;
  }

  switch (deleteState_) {
    case DeleteState::DeleteAllConfirm:
    case DeleteState::DeleteSingleConfirm: {
      if (e == BtnEvent::Double) {
        deleteState_ = DeleteState::Idle;
        enterDeleteMenu_();
        clearHelperOverlay_();
        return;
      }
      if (e == BtnEvent::Single) {
        deleteConfirmSelection_ = (deleteConfirmSelection_ + 1) % 3;
        markDeleteConfirmDirty_();
      } else if (e == BtnEvent::Long) {
        if (deleteConfirmSelection_ == 1) {
          if (deleteState_ == DeleteState::DeleteAllConfirm) {
            confirmDeleteAll_();
          } else {
            confirmDeleteSingle_();
          }
        } else if (deleteConfirmSelection_ == 0) {
          if (deleteState_ == DeleteState::DeleteAllConfirm) {
            cancelDeleteAll_();
          } else {
            cancelDeleteSingle_();
          }
        } else {
          deleteState_ = DeleteState::Idle;
          returnToSlideshowMenu_();
        }
      }
      return;
    }

    case DeleteState::DeleteSingle:
      if (e == BtnEvent::Double) {
        deleteState_ = DeleteState::Idle;
        enterDeleteMenu_();
        clearHelperOverlay_();
      } else if (e == BtnEvent::Single) {
        advance_(+1);
        showHelperOverlay_(
            "BTN2 lang: Löschen",
            "BTN2 kurz: Nächstes Bild",
            "BTN2 doppelt: Exit",
            kDeleteHintDurationMs);
      } else if (e == BtnEvent::Long) {
        requestDeleteSingle_();
      }
      return;

    default:
      break;
  }

  switch (e) {
    case BtnEvent::Single:
      if (controlMode_ == ControlMode::DeleteMenu) {
        deleteMenuSelection_ = (deleteMenuSelection_ + 1) % 3;
        markDeleteMenuDirty_();
      } else {
        advance_(+1);
      }
      break;

    case BtnEvent::Double:
      if (controlMode_ == ControlMode::DeleteMenu) {
        returnToSlideshowMenu_();
      }
      break;

    case BtnEvent::Triple:
      if (controlMode_ == ControlMode::DeleteMenu) {
        showToast_(i18n.t("buttons.long_auto"), kToastShortMs);
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
          showToast_(i18n.t("slideshow.no_images"), kToastShortMs);
        } else {
          setControlMode_(ControlMode::Manual);
        }
      } else if (controlMode_ == ControlMode::Manual) {
        openSlideshowMenu_();
      } else if (controlMode_ == ControlMode::DeleteMenu) {
        if (deleteMenuSelection_ == 0) {
          requestDeleteAll_();
        } else if (deleteMenuSelection_ == 1) {
          startDeleteSingle_();
        } else {
          returnToSlideshowMenu_();
        }
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
  if (menuScreen_ == MenuScreen::Slideshow) {
    drawSlideshowMenu_();
    return;
  }
  if (menuScreen_ == MenuScreen::Source) {
    drawSourceMenu_();
    return;
  }
  if (menuScreen_ == MenuScreen::AutoSpeed) {
    drawAutoSpeedMenu_();
    return;
  }
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

  // If no images available, show message (allows BTN1 to work for app switching)
  static bool emptyMessageShown = false;
  if (files_.empty() && controlMode_ != ControlMode::DeleteMenu) {
    // Only redraw if needed (prevents flickering)
    if (!emptyMessageShown) {
      tft.fillScreen(TFT_BLACK);
      const int16_t line = TextRenderer::lineHeight();
      const int16_t centerY = (TFT_H - line * 2) / 2;
      TextRenderer::drawCentered(centerY, i18n.t("slideshow.no_images"), TFT_WHITE, TFT_BLACK);
      TextRenderer::drawCentered(centerY + line, i18n.t("slideshow.found"), TFT_WHITE, TFT_BLACK);
      emptyMessageShown = true;
    }
  } else {
    emptyMessageShown = false;  // Reset flag when files become available
  }

  // Play GIF frames continuously if a GIF is active
  if (gifPlaying_) {
    tft.startWrite();
    if (!gif_.playFrame(true, NULL)) {
      // End of GIF reached - loop back to start
      gif_.reset();
    }
    tft.endWrite();
  }

  drawManualFilenameOverlay_();
  drawToastOverlay_();
  drawHelperOverlay_();
}

void SlideshowApp::shutdown() {
  stopGif_();
  files_.clear();
}

void SlideshowApp::resume() {
  // Called when returning from system setup menu
  // SystemUI already cleared the screen with fillScreen(BLACK)
  // so we need to redraw the current image
  if (!files_.empty()) {
    showCurrent_(true, true);
  }
}

// ============================================================================
// GIF Support Implementation
// ============================================================================

// Static file handle for GIF callbacks
static File gifFile;

// Static instance pointer for accessing member variables from static callbacks
static SlideshowApp* currentSlideshowInstance = nullptr;

void* SlideshowApp::gifOpen_(const char* fname, int32_t* pSize) {
  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] Opening GIF: %s\n", fname);
  #endif

  gifFile = LittleFS.open(fname, "r");
  if (!gifFile) {
    // Try SD card
    gifFile = SD.open(fname, FILE_READ);
  }

  if (gifFile) {
    *pSize = gifFile.size();
    #ifdef USB_DEBUG
      Serial.printf("[Slideshow] GIF size: %d bytes\n", *pSize);
    #endif
    return (void*)&gifFile;
  }

  #ifdef USB_DEBUG
    Serial.println(F("[Slideshow] Failed to open GIF file"));
  #endif
  return NULL;
}

void SlideshowApp::gifClose_(void* pHandle) {
  if (gifFile) {
    gifFile.close();
  }
}

int32_t SlideshowApp::gifRead_(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  int32_t bytesRead = gifFile.read(pBuf, iLen);
  pFile->iPos = gifFile.position();
  return bytesRead;
}

int32_t SlideshowApp::gifSeek_(GIFFILE* pFile, int32_t iPosition) {
  gifFile.seek(iPosition);
  pFile->iPos = iPosition;
  return iPosition;
}

void SlideshowApp::gifDraw_(GIFDRAW* pDraw) {
  if (!currentSlideshowInstance) return;

  uint8_t *s;
  uint16_t *d, *usPalette;
  int x, y, iWidth;

  usPalette = pDraw->pPalette;

  // Lazily initialize canvas bounds if decoder did not provide them earlier
  if (currentSlideshowInstance->gifCanvasW_ <= 0 || currentSlideshowInstance->gifCanvasH_ <= 0) {
    currentSlideshowInstance->gifCanvasW_ = pDraw->iWidth;
    currentSlideshowInstance->gifCanvasH_ = pDraw->iHeight;
    currentSlideshowInstance->gifOffsetX_ = (TFT_W - currentSlideshowInstance->gifCanvasW_) / 2;
    currentSlideshowInstance->gifOffsetY_ = (TFT_H - currentSlideshowInstance->gifCanvasH_) / 2;
  }

  if (pDraw->y == 0) {
    int clearX = currentSlideshowInstance->gifOffsetX_;
    int clearY = currentSlideshowInstance->gifOffsetY_;
    int clearW = currentSlideshowInstance->gifCanvasW_;
    int clearH = currentSlideshowInstance->gifCanvasH_;

    if (clearW > 0 && clearH > 0) {
      if (clearX < 0) {
        clearW += clearX;
        clearX = 0;
      }
      if (clearY < 0) {
        clearH += clearY;
        clearY = 0;
      }
      if (clearX < TFT_W && clearY < TFT_H) {
        if (clearX + clearW > TFT_W) clearW = TFT_W - clearX;
        if (clearY + clearH > TFT_H) clearH = TFT_H - clearY;
        if (clearW > 0 && clearH > 0) {
          tft.fillRect(clearX, clearY, clearW, clearH, TFT_BLACK);
        }
      }
    }
  }

  y = pDraw->iY + pDraw->y + currentSlideshowInstance->gifOffsetY_;
  x = pDraw->iX + currentSlideshowInstance->gifOffsetX_;

  // Clip to display bounds
  if (y >= TFT_H || y < 0 || x >= TFT_W) return;

  s = pDraw->pPixels;
  iWidth = pDraw->iWidth;

  // Clip width to display
  if (iWidth + x > TFT_W) {
    iWidth = TFT_W - x;
  }
  if (x < 0) {
    s += (-x);
    iWidth += x;
    x = 0;
  }
  if (iWidth <= 0) return;

  // Set drawing window for this scanline
  tft.setAddrWindow(x, y, iWidth, 1);

  // Handle disposal method 2 (restore to background)
  if (pDraw->ucDisposalMethod == 2) {
    for (x = 0; x < iWidth; x++) {
      if (s[x] == pDraw->ucTransparent) {
        s[x] = pDraw->ucBackground;
      }
    }
    pDraw->ucHasTransparency = 0;
  }

  // Convert palette indices to RGB565 and push to display
  if (pDraw->ucHasTransparency) {
    // Slow path: handle transparent pixels
    uint16_t usTemp[iWidth];
    int i, count = 0;

    for (i = 0; i < iWidth; i++) {
      if (s[i] != pDraw->ucTransparent) {
        usTemp[count++] = usPalette[s[i]];
      } else {
        if (count > 0) {
          tft.pushPixels(usTemp, count);
          count = 0;
        }
      }
    }

    if (count > 0) {
      tft.pushPixels(usTemp, count);
    }

  } else {
    // Fast path: no transparency
    uint16_t usTemp[iWidth];
    for (int i = 0; i < iWidth; i++) {
      usTemp[i] = usPalette[s[i]];
    }
    tft.pushPixels(usTemp, iWidth);
  }
}

void SlideshowApp::showCurrentGif_(bool allowManualOverlay, bool clearScreen) {
  if (files_.empty()) return;

  const String& path = files_[idx_];

  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] Displaying GIF: %s\n", path.c_str());
  #endif

  // If already playing this GIF, don't reopen
  if (gifPlaying_ && currentGifPath_ == path) {
    return;
  }

  // Stop any currently playing GIF
  stopGif_();

  // Step 1: Clear screen
  tft.fillScreen(TFT_BLACK);

  // Step 2: Show filename for 2 seconds (only in Manual mode)
  if (controlMode_ == ControlMode::Manual && allowManualOverlay) {
    String base = path;
    int sidx = base.lastIndexOf('/');
    if (sidx >= 0) base = base.substring(sidx + 1);
    String displayName = base;
    String lower = base;
    lower.toLowerCase();
    if (lower.endsWith(".gif")) {
      displayName = base.substring(0, base.length() - 4);
    }

    // Display filename in center
    TextRenderer::drawCentered(TFT_H / 2, displayName, TFT_WHITE, TFT_BLACK);

    // Wait 2 seconds
    delay(2000);

    // Step 3: Clear screen again
    tft.fillScreen(TFT_BLACK);
  }

  // Reset GIF canvas dimensions to force recalculation
  gifCanvasW_ = 0;
  gifCanvasH_ = 0;
  gifOffsetX_ = 0;
  gifOffsetY_ = 0;

  // Set the instance pointer for static callbacks
  currentSlideshowInstance = this;

  // Step 4: Open and start GIF
  if (!gif_.open(path.c_str(), gifOpen_, gifClose_, gifRead_, gifSeek_, gifDraw_)) {
    #ifdef USB_DEBUG
      Serial.println(F("[Slideshow] Failed to open GIF"));
    #endif
    return;
  }

  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] GIF opened: %dx%d\n",
                  gif_.getCanvasWidth(), gif_.getCanvasHeight());
  #endif

  // Cache the logical GIF canvas dimensions for consistent centering
  gifCanvasW_ = gif_.getCanvasWidth();
  gifCanvasH_ = gif_.getCanvasHeight();
  if (gifCanvasW_ <= 0 || gifCanvasH_ <= 0) {
    // Fallback: first decoded frame will provide the size
    gifCanvasW_ = 0;
    gifCanvasH_ = 0;
    gifOffsetX_ = 0;
    gifOffsetY_ = 0;
  } else {
    gifOffsetX_ = (TFT_W - gifCanvasW_) / 2;
    gifOffsetY_ = (TFT_H - gifCanvasH_) / 2;
  }

  gifPlaying_ = true;
  currentGifPath_ = path;

  // Don't use the manualFilename overlay system for GIFs
  manualFilenameActive_ = false;
  manualFilenameLabel_.clear();
  manualFilenameUntil_ = 0;

  if (toastUntil_) {
    toastDirty_ = true;
  }
  drawToastOverlay_();

  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] GIF opened (%s): %s\n", slideSourceLabel(source_), path.c_str());
  #endif
}

void SlideshowApp::stopGif_() {
  if (gifPlaying_) {
    gif_.close();
    if (gifFile) {
      gifFile.close();
    }
    gifPlaying_ = false;
    currentGifPath_.clear();

    // Clear instance pointer
    if (currentSlideshowInstance == this) {
      currentSlideshowInstance = nullptr;
    }
  }
}
