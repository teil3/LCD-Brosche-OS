#include "SlideshowApp.h"

#include <algorithm>
#include <cstdio>

#include "Core/Gfx.h"
#include "Config.h"
#include "Core/TextRenderer.h"
#include "Core/Storage.h"
#include "Core/BleImageTransfer.h"
#include "Core/SerialImageTransfer.h"

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

void SlideshowApp::onBleTransferStarted(const char* filename, size_t size) {
  handleTransferStarted_(TransferSource::Ble, filename, size);
}

void SlideshowApp::onUsbTransferStarted(const char* filename, size_t size) {
  handleTransferStarted_(TransferSource::Usb, filename, size);
}

void SlideshowApp::onBleTransferCompleted(const char* filename, size_t size) {
  handleTransferCompleted_(TransferSource::Ble, filename, size);
}

void SlideshowApp::onUsbTransferCompleted(const char* filename, size_t size) {
  handleTransferCompleted_(TransferSource::Usb, filename, size);
}

void SlideshowApp::onBleTransferError(const char* message) {
  handleTransferError_(TransferSource::Ble, message);
}

void SlideshowApp::onBleTransferAborted(const char* message) {
  handleTransferAborted_(TransferSource::Ble, message);
}

void SlideshowApp::onUsbTransferError(const char* message) {
  handleTransferError_(TransferSource::Usb, message);
}

void SlideshowApp::onUsbTransferAborted(const char* message) {
  handleTransferAborted_(TransferSource::Usb, message);
}

const char* SlideshowApp::transferLabel_(TransferSource src) const {
  switch (src) {
    case TransferSource::Usb: return "USB";
    case TransferSource::Ble: return "BLE";
    default:                  return "Transfer";
  }
}

void SlideshowApp::handleTransferStarted_(TransferSource src, const char* filename, size_t size) {
  if (copyState_ == CopyState::Running) return;
  const char* label = transferLabel_(src);
  if (controlMode_ != ControlMode::BleReceive) {
    showToast_(String(label) + String(": Modus aktivieren"), 1200);
    return;
  }

  transferSource_ = src;
  bleState_ = BleState::Receiving;
  bleLastFilename_ = filename ? String(filename) : String();
  bleLastBytesExpected_ = size;
  bleLastBytesReceived_ = 0;
  if (bleLastFilename_.isEmpty()) {
    bleLastMessage_ = String(label) + String(": Empfang läuft");
  } else {
    bleLastMessage_ = String(label) + String(": ") + bleLastFilename_;
  }
  bleOverlayDirty_ = true;
}

void SlideshowApp::handleTransferCompleted_(TransferSource src, const char* filename, size_t size) {
  if (copyState_ == CopyState::Running) return;

  const char* label = transferLabel_(src);
  bool inTransferMode = (controlMode_ == ControlMode::BleReceive);
  if (!ensureFlashReady_()) {
    if (inTransferMode) {
      transferSource_ = src;
      bleState_ = BleState::Error;
      bleLastMessage_ = "Flash-Fehler";
      bleOverlayDirty_ = true;
    } else {
      showToast_(String(label) + String(": Flash Fehler"), 1800);
    }
    return;
  }

  if (!rebuildFileListFrom_(SlideSource::Flash)) {
    if (inTransferMode) {
      transferSource_ = src;
      bleState_ = BleState::Error;
      bleLastMessage_ = "Keine Flash-Bilder";
      bleOverlayDirty_ = true;
    } else {
      showToast_(String(label) + String(": Keine Flash-Bilder"), 1500);
    }
    return;
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

  if (inTransferMode) {
    transferSource_ = src;
    bleState_ = BleState::Completed;
    bleLastFilename_ = filename ? String(filename) : String();
    bleLastBytesExpected_ = size;
    bleLastBytesReceived_ = size;
    bleLastMessage_ = bleLastFilename_.isEmpty() ? String("Empfang abgeschlossen")
                                                 : String("Fertig: ") + bleLastFilename_;
    bleOverlayDirty_ = true;
    bleLastHeader_.clear();
    bleLastPrimary_.clear();
    bleLastSecondary_.clear();
    bleLastFooter_.clear();
  } else {
    String msg = String(label) + String(" fertig");
    if (filename && filename[0]) {
      msg += ": ";
      msg += filename;
    }
    if (size > 0) {
      uint32_t kb = (static_cast<uint32_t>(size) + 1023) / 1024;
      msg += " (";
      msg += kb;
      msg += " KB)";
    }
    showToast_(msg, 1800);
  }
}

void SlideshowApp::handleTransferError_(TransferSource src, const char* message) {
  if (copyState_ == CopyState::Running) return;
  const char* label = transferLabel_(src);
  if (controlMode_ == ControlMode::BleReceive) {
    transferSource_ = src;
    bleState_ = BleState::Error;
    bleLastMessage_ = message ? String(message) : String("Fehler");
    bleOverlayDirty_ = true;
  } else {
    String msg = String(label) + String(" Fehler");
    if (message && message[0]) {
      msg += ": ";
      msg += message;
    }
    showToast_(msg, 1800);
  }
}

void SlideshowApp::handleTransferAborted_(TransferSource src, const char* message) {
  if (copyState_ == CopyState::Running) return;
  const char* label = transferLabel_(src);
  if (controlMode_ == ControlMode::BleReceive) {
    transferSource_ = src;
    bleState_ = BleState::Aborted;
    bleLastMessage_ = message ? String(message) : String("Abgebrochen");
    bleOverlayDirty_ = true;
  } else {
    String msg = String(label) + String(" abgebrochen");
    if (message && message[0]) {
      msg += ": ";
      msg += message;
    }
    showToast_(msg, 1200);
  }
}

void SlideshowApp::setControlMode_(ControlMode mode, bool showToast) {
  if (copyState_ == CopyState::Running) return;
  ControlMode previous = controlMode_;
  if (previous == mode) return;

  if (previous == ControlMode::BleReceive) {
    BleImageTransfer::setTransferEnabled(false);
    SerialImageTransfer::setTransferEnabled(false);
    bleState_ = BleState::Idle;
    bleLastBytesExpected_ = 0;
    bleLastBytesReceived_ = 0;
    bleOverlayNeedsClear_ = true;
    bleProgressFrameDrawn_ = false;
    bleBarFill_ = 0;
    bleLastHeader_.clear();
    bleLastPrimary_.clear();
    bleLastSecondary_.clear();
    bleLastFooter_.clear();
    transferSource_ = TransferSource::None;
  }

  controlMode_ = mode;
  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  }
  if (controlMode_ == ControlMode::BleReceive) {
    bleState_ = BleState::Idle;
    transferSource_ = TransferSource::None;
    bleLastMessage_.clear();
    bleLastFilename_.clear();
    bleLastBytesExpected_ = 0;
    bleLastBytesReceived_ = 0;
    bleOverlayDirty_ = true;
    BleImageTransfer::setTransferEnabled(true);
    SerialImageTransfer::setTransferEnabled(true);
    bleOverlayNeedsClear_ = true;
    bleProgressFrameDrawn_ = false;
    bleBarFill_ = 0;
    bleLastHeader_.clear();
    bleLastPrimary_.clear();
    bleLastSecondary_.clear();
    bleLastFooter_.clear();
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

    case ControlMode::BleReceive:
      auto_mode = false;
      toastText_.clear();
      toastUntil_ = 0;
      bleOverlayDirty_ = true;
      bleOverlayNeedsClear_ = true;
      break;

    case ControlMode::DeleteMenu:
      auto_mode = false;
      toastText_.clear();
      toastUntil_ = 0;
      deleteState_ = DeleteState::Idle;
      deleteMenuSelection_ = 0;
      markDeleteMenuDirty_();
      break;
  }
  timeSinceSwitch_ = 0;

  if ((previous == ControlMode::StorageMenu || previous == ControlMode::BleReceive || previous == ControlMode::DeleteMenu) &&
      controlMode_ != ControlMode::StorageMenu && controlMode_ != ControlMode::BleReceive && controlMode_ != ControlMode::DeleteMenu) {
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
    #ifdef USB_DEBUG
      Serial.printf("[Slideshow] prepareCopyQueue: readDirectoryEntries failed for '%s'\n", dir.c_str());
    #endif
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
      #ifdef USB_DEBUG
        Serial.printf("[Slideshow] copy queue open fail: %s\n", path.c_str());
      #endif
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
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] prepareCopyQueue: no JPEG files on SD");
    #endif
    return false;
  }

  if (!ensureFlashReady_()) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] prepareCopyQueue: ensureFlashReady_ failed");
    #endif
    return false;
  }

  std::sort(items.begin(), items.end(), [](const CopyItem& a, const CopyItem& b) {
    return a.name < b.name;
  });

  if (!clearFlashSlidesDir()) {
    #ifdef USB_DEBUG
      Serial.println("[Slideshow] WARN: Flash clear failed (continue)");
    #endif
  }

  copyQueue_ = std::move(items);
  for (const auto& item : copyQueue_) {
    copyBytesTotal_ += item.size;
  }
  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] prepareCopyQueue: %u files, total=%u bytes\n",
                  static_cast<unsigned>(copyQueue_.size()),
                  static_cast<unsigned>(copyBytesTotal_));
  #endif
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

void SlideshowApp::performDeleteAll_() {
  if (!ensureFlashReady_()) {
    deleteState_ = DeleteState::Error;
    showToast_("Flash-Fehler", kCopyErrorToastMs);
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
  showToast_(msg, kCopyDoneToastMs);

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

void SlideshowApp::showCurrent_() {
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

  tft.fillScreen(TFT_BLACK);

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

  #ifdef USB_DEBUG
    Serial.printf("[Slideshow] OK (%s): %s\n", slideSourceLabel(source_), path.c_str());
  #endif

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
    case ControlMode::BleReceive:
      return String("TRANSFER");
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
  if (controlMode_ == ControlMode::StorageMenu) {
    markStorageMenuDirty_();
  } else if (controlMode_ == ControlMode::BleReceive) {
    bleOverlayDirty_ = true;
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
  String footerLine = toastActive ? toastText_ : String("Lang: Transfer-Modus");

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
  TextRenderer::drawCentered(doubleY, "Doppel: SD-Kopie", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(footerY, footerLine, TFT_WHITE, TFT_BLACK);
}

void SlideshowApp::markStorageMenuDirty_() {
  storageMenuDirty_ = true;
}

void SlideshowApp::markCopyConfirmDirty_() {
  copyConfirmDirty_ = true;
  copyConfirmLastSelection_ = 255;
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

void SlideshowApp::drawBleReceiveOverlay_() {
  if (bleState_ == BleState::Receiving) {
    size_t expected = 0;
    size_t received = 0;
    if (transferSource_ == TransferSource::Usb) {
      expected = SerialImageTransfer::bytesExpected();
      received = SerialImageTransfer::bytesReceived();
    } else {
      expected = BleImageTransfer::bytesExpected();
      received = BleImageTransfer::bytesReceived();
    }
    if (expected > 0 && expected != bleLastBytesExpected_) {
      bleLastBytesExpected_ = expected;
      bleOverlayDirty_ = true;
    }
    if (received != bleLastBytesReceived_) {
      bleLastBytesReceived_ = received;
      bleOverlayDirty_ = true;
    }
  }

  const int16_t line = TextRenderer::lineHeight();
  const int16_t headerY = 44;
  const int16_t primaryY = headerY + line + 12;
  const int16_t secondaryY = primaryY + line + 10;
  const int16_t barWidth = tft.width() - 48;
  const int16_t barHeight = 14;
  const int16_t barX = (tft.width() - barWidth) / 2;
  const int16_t barY = secondaryY + line + 18;
  const int16_t footerY = tft.height() - line - 36;

  if (bleOverlayNeedsClear_) {
    tft.fillScreen(TFT_BLACK);
    bleOverlayNeedsClear_ = false;
    bleProgressFrameDrawn_ = false;
    bleBarFill_ = 0;
    bleLastHeader_.clear();
    bleLastPrimary_.clear();
    bleLastSecondary_.clear();
    bleLastFooter_.clear();
  }

  auto formatKB = [](size_t bytes) -> String {
    if (bytes < 1024) {
      uint32_t tenth = static_cast<uint32_t>(((bytes * 10UL) + 512UL) / 1024UL);
      if (tenth >= 10) {
        return String("1.0");
      }
      char buf[16];
      std::snprintf(buf, sizeof(buf), "0.%u", tenth);
      return String(buf);
    }
    uint32_t whole = bytes / 1024;
    uint32_t tenth = static_cast<uint32_t>(((bytes % 1024) * 10UL) / 1024UL);
    if (tenth == 0) {
      return String(whole);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u", whole, tenth);
    return String(buf);
  };

  bool allowClear = (bleState_ != BleState::Completed);

  String header;
  switch (transferSource_) {
    case TransferSource::Usb:
      header = "USB-Transfer";
      break;
    case TransferSource::Ble:
      header = "Bluetooth Transfer";
      break;
    default:
      header = "Übertragung (USB/BLE)";
      break;
  }
  if (allowClear) {
    if (header != bleLastHeader_) {
      tft.fillRect(0, headerY - 4, tft.width(), line + 8, TFT_BLACK);
      TextRenderer::drawCentered(headerY, header, TFT_WHITE, TFT_BLACK);
      bleLastHeader_ = header;
    }
  } else if (header != bleLastHeader_) {
    TextRenderer::drawCentered(headerY, header, TFT_WHITE, TFT_BLACK);
    bleLastHeader_ = header;
  }

  String primary;
  String secondary;

  switch (bleState_) {
    case BleState::Idle:
      primary = "Im Tool \"Über USB\" oder \"Per Bluetooth\" starten";
      secondary = "Gerät wartet auf neue Übertragung";
      break;
    case BleState::Receiving: {
      const char* label = transferLabel_(transferSource_);
      primary = bleLastFilename_.isEmpty()
                  ? String(label) + String(": Empfang läuft")
                  : String(label) + String(": ") + bleLastFilename_;
      if (bleLastBytesExpected_ > 0) {
        uint32_t pct = static_cast<uint32_t>((bleLastBytesReceived_ * 100UL) / bleLastBytesExpected_);
        String recv = formatKB(bleLastBytesReceived_);
        String total = formatKB(bleLastBytesExpected_);
        secondary = String(pct) + String("% (") + recv + String("/") + total + String(" KB)");
      } else {
        secondary = String(bleLastBytesReceived_) + String(" B erhalten");
      }
      break;
    }
    case BleState::Completed: {
      const char* label = transferLabel_(transferSource_);
      primary = String(label) + String(": Empfangen fertig");
      secondary = bleLastFilename_.isEmpty() ? bleLastMessage_ : bleLastFilename_;
      break;
    }
    case BleState::Error: {
      const char* label = transferLabel_(transferSource_);
      primary = String(label) + String(": Fehler");
      secondary = bleLastMessage_.isEmpty() ? String("Übertragung fehlgeschlagen") : bleLastMessage_;
      break;
    }
    case BleState::Aborted: {
      const char* label = transferLabel_(transferSource_);
      primary = String(label) + String(": Abgebrochen");
      secondary = bleLastMessage_.isEmpty() ? String("Übertragung abgebrochen") : bleLastMessage_;
      break;
    }
  }

  if (allowClear) {
    if (primary != bleLastPrimary_) {
      tft.fillRect(0, primaryY - 4, tft.width(), line + 8, TFT_BLACK);
      TextRenderer::drawCentered(primaryY, primary, TFT_WHITE, TFT_BLACK);
      bleLastPrimary_ = primary;
    }
    if (secondary != bleLastSecondary_) {
      tft.fillRect(0, secondaryY - 4, tft.width(), line + 8, TFT_BLACK);
      if (!secondary.isEmpty()) {
        TextRenderer::drawCentered(secondaryY, secondary, TFT_WHITE, TFT_BLACK);
      }
      bleLastSecondary_ = secondary;
    }
  } else {
    if (primary != bleLastPrimary_) {
      TextRenderer::drawCentered(primaryY, primary, TFT_WHITE, TFT_BLACK);
      bleLastPrimary_ = primary;
    }
    if (secondary != bleLastSecondary_) {
      if (!secondary.isEmpty()) {
        TextRenderer::drawCentered(secondaryY, secondary, TFT_WHITE, TFT_BLACK);
      }
      bleLastSecondary_ = secondary;
    }
  }

  if (bleState_ == BleState::Receiving && bleLastBytesExpected_ > 0) {
    size_t denom = bleLastBytesExpected_ ? bleLastBytesExpected_ : 1;
    uint16_t fill = static_cast<uint16_t>(((barWidth - 2) * bleLastBytesReceived_) / denom);
    if (!bleProgressFrameDrawn_) {
      tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
      bleProgressFrameDrawn_ = true;
      bleBarFill_ = 0;
    }
    if (fill != bleBarFill_) {
      tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, TFT_BLACK);
      if (fill > 0) {
        if (fill > barWidth - 2) fill = barWidth - 2;
        tft.fillRect(barX + 1, barY + 1, fill, barHeight - 2, TFT_WHITE);
      }
      bleBarFill_ = fill;
    }
  } else {
    if (bleProgressFrameDrawn_ || bleBarFill_ != 0) {
      if (allowClear) {
        tft.fillRect(barX, barY, barWidth, barHeight, TFT_BLACK);
      }
      bleProgressFrameDrawn_ = false;
      bleBarFill_ = 0;
    }
  }

  String footer;
  if (toastUntil_ && millis() < toastUntil_) {
    footer = toastText_;
  }
  if (footer.isEmpty()) {
    footer = "Lang: Löschmodus";
  }
  if (allowClear) {
    if (footer != bleLastFooter_) {
      tft.fillRect(0, footerY - 4, tft.width(), line + 8, TFT_BLACK);
      TextRenderer::drawCentered(footerY, footer, TFT_WHITE, TFT_BLACK);
      bleLastFooter_ = footer;
    }
  } else if (footer != bleLastFooter_) {
    TextRenderer::drawCentered(footerY, footer, TFT_WHITE, TFT_BLACK);
    bleLastFooter_ = footer;
  }

  bleOverlayDirty_ = false;
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
  source_ = SlideSource::SDCard;
  copyState_ = CopyState::Idle;
  copyAbortRequest_ = false;
  markCopyConfirmDirty_();
  storageMenuDirty_ = true;
  storageMenuLastSource_.clear();
  storageMenuLastFooter_.clear();
  storageMenuLastToastActive_ = false;
  bleState_ = BleState::Idle;
  bleOverlayDirty_ = true;
  bleLastMessage_.clear();
  bleLastFilename_.clear();
  bleLastBytesExpected_ = 0;
  bleLastBytesReceived_ = 0;
  BleImageTransfer::setTransferEnabled(false);
  SerialImageTransfer::setTransferEnabled(false);
  bleOverlayNeedsClear_ = true;
  bleProgressFrameDrawn_ = false;
  bleBarFill_ = 0;
  bleLastHeader_.clear();
  bleLastPrimary_.clear();
  bleLastSecondary_.clear();
  bleLastFooter_.clear();
  transferSource_ = TransferSource::None;
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

  if (controlMode_ == ControlMode::BleReceive) {
    if (bleState_ == BleState::Receiving) {
      size_t expected = 0;
      size_t received = 0;
      if (transferSource_ == TransferSource::Usb) {
        expected = SerialImageTransfer::bytesExpected();
        received = SerialImageTransfer::bytesReceived();
      } else {
        expected = BleImageTransfer::bytesExpected();
        received = BleImageTransfer::bytesReceived();
      }
      if (expected > 0) {
        bleLastBytesExpected_ = expected;
      }
      if (received != bleLastBytesReceived_) {
        bleLastBytesReceived_ = received;
        bleOverlayDirty_ = true;
      }
    }
    return;
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
      } else if (controlMode_ == ControlMode::BleReceive) {
        String msg = (bleState_ == BleState::Receiving)
                     ? String("Übertragung läuft")
                     : String("Bereit für Browser");
        showToast_(msg, kToastShortMs);
      } else if (controlMode_ == ControlMode::DeleteMenu) {
        deleteMenuSelection_ ^= 1;
        markDeleteMenuDirty_();
      } else {
        advance_(+1);
      }
      break;

    case BtnEvent::Double:
      if (controlMode_ == ControlMode::StorageMenu) {
        requestCopy_();
      } else if (controlMode_ == ControlMode::BleReceive) {
        bleOverlayDirty_ = true;
        showToast_(bleLastMessage_.isEmpty() ? String("Warte auf Senden") : bleLastMessage_, kToastShortMs);
      } else if (controlMode_ == ControlMode::DeleteMenu) {
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
      if (controlMode_ == ControlMode::StorageMenu) {
        showToast_("Lang: Modus verlassen", kToastShortMs);
      } else if (controlMode_ == ControlMode::BleReceive) {
        bleOverlayDirty_ = true;
        showToast_("Lang: Löschmodus", kToastShortMs);
      } else if (controlMode_ == ControlMode::DeleteMenu) {
        showToast_("Lang: Auto", kToastShortMs);
      } else {
        show_filename = !show_filename;
        showCurrent_();
      }
      break;

    case BtnEvent::Long: {
      if (controlMode_ == ControlMode::Auto) {
        if (files_.empty()) {
          setControlMode_(ControlMode::StorageMenu);
        } else {
          setControlMode_(ControlMode::Manual);
        }
      } else if (controlMode_ == ControlMode::Manual) {
        setControlMode_(ControlMode::StorageMenu);
      } else if (controlMode_ == ControlMode::StorageMenu) {
        setControlMode_(ControlMode::BleReceive);
      } else if (controlMode_ == ControlMode::BleReceive) {
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
  if (copyState_ == CopyState::Running) {
    drawCopyOverlay_();
    return;
  }
  if (copyState_ == CopyState::Confirm) {
    drawCopyConfirmOverlay_();
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
  if (controlMode_ == ControlMode::StorageMenu) {
    drawStorageMenuOverlay_();
    return;
  }
  if (controlMode_ == ControlMode::BleReceive) {
    drawBleReceiveOverlay_();
    return;
  }
  drawToastOverlay_();
}

void SlideshowApp::shutdown() {
  files_.clear();
  closeCopyFiles_();
  copyQueue_.clear();
  BleImageTransfer::setTransferEnabled(false);
  SerialImageTransfer::setTransferEnabled(false);
  transferSource_ = TransferSource::None;
}
