#include "SystemUI.h"

#include <algorithm>
#include <utility>

#include "Core/Gfx.h"
#include "Config.h"
#include <cstdio>

#include "Core/TextRenderer.h"
#include "Core/BleImageTransfer.h"
#include "Core/SerialImageTransfer.h"
#include "Core/I18n.h"

namespace {
constexpr uint32_t kSdCopyTickBudgetMs = 25;
constexpr uint32_t kCopyAbortStatusMs = 1500;
constexpr uint32_t kCopyDoneStatusMs = 1500;
constexpr uint32_t kCopyErrorStatusMs = 1800;
}

void SystemUI::begin(const Callbacks& cb) {
  callbacks_ = cb;
  resetTransferUi_();
}

void SystemUI::showSetup() {
  resetSdCopyUi_();
  resetTransferUi_();
  activeScreen_ = Screen::Setup;
  setupMenu_.show();
}

void SystemUI::hide() {
  if (activeScreen_ == Screen::Setup) {
    setupMenu_.hide();
  } else if (activeScreen_ == Screen::SdCopyConfirm) {
    sdCopyCancel_();
    resetSdCopyUi_();
  } else if (activeScreen_ == Screen::SdCopyProgress) {
    sdCopyAbort_();
    resetSdCopyUi_();
  } else if (activeScreen_ == Screen::Transfer) {
    resetTransferUi_();
  }
  activeScreen_ = Screen::None;
}

bool SystemUI::handleButton(uint8_t index, BtnEvent e) {
  if (!isActive()) return false;
  bool handled = false;
  switch (activeScreen_) {
    case Screen::Setup:
      handled = handleSetupButtons_(index, e);
      break;
    case Screen::Language:
      handled = handleLanguageButtons_(index, e);
      break;
    case Screen::SdCopyConfirm:
      handled = handleSdCopyConfirmButtons_(index, e);
      break;
    case Screen::SdCopyProgress:
      handled = handleSdCopyProgressButtons_(index, e);
      break;
    case Screen::Transfer:
      handled = handleTransferButtons_(index, e);
      break;
    case Screen::None:
    default:
      break;
  }
  return handled || isActive();
}

bool SystemUI::handleSetupButtons_(uint8_t index, BtnEvent e) {
  if (index == 1) {
    if (e == BtnEvent::Double) {
      hide();
      return true;
    }
    if (e == BtnEvent::Single) {
      hide();
      return true;
    }
    return false;
  }
  if (index != 2) {
    return false;
  }
  switch (e) {
    case BtnEvent::Single:
      setupMenu_.next();
      return true;
    case BtnEvent::Double:
      hide();
      return true;
    case BtnEvent::Long:
      handleSetupSelection_();
      return true;
    default:
      break;
  }
  return false;
}

void SystemUI::handleSetupSelection_() {
  SetupMenu::Item item = setupMenu_.currentItem();
  bool requiresSlideshow =
      (item == SetupMenu::Item::UsbBleTransfer || item == SetupMenu::Item::SdTransfer);
  if (requiresSlideshow) {
    if (!callbacks_.ensureSlideshowActive || !callbacks_.ensureSlideshowActive()) {
      setupMenu_.showStatus(i18n.t("system.slideshow_unavailable"), 1500);
      return;
    }
  }

  bool shouldHide = true;
  switch (item) {
    case SetupMenu::Item::Language:
      shouldHide = false;
      showLanguageSelection_();
      break;
    case SetupMenu::Item::UsbBleTransfer:
      shouldHide = false;
      if (!showTransferScreen_()) {
        setupMenu_.showStatus(i18n.t("system.transfer_impossible"), 1500);
      }
      break;
    case SetupMenu::Item::SdTransfer:
      shouldHide = false;
      if (!sdCopyStartConfirm_()) {
        setupMenu_.showStatus(i18n.t("system.copy_running"), 1500);
        break;
      }
      showSdCopyConfirm_();
      break;
    case SetupMenu::Item::Exit:
    case SetupMenu::Item::Count:
      break;
  }

  if (shouldHide) {
    hide();
  }
}

void SystemUI::draw() {
  switch (activeScreen_) {
    case Screen::Setup:
      setupMenu_.draw();
      break;
    case Screen::Language:
      drawLanguageSelection_();
      break;
    case Screen::SdCopyConfirm:
      drawSdCopyConfirm_();
      break;
    case Screen::SdCopyProgress:
      drawSdCopyProgress_();
      break;
    case Screen::Transfer:
      drawTransfer_();
      break;
    default:
      break;
  }
}

void SystemUI::showSdCopyConfirm_() {
  sdCopyStartConfirm_();
  sdCopySelection_ = 1;
  sdCopyDirty_ = true;
  sdCopyStatusText_.clear();
  sdCopyStatusUntil_ = 0;
  sdCopyStatusColor_ = TFT_WHITE;
  sdCopyPendingExit_ = false;
  sdCopyLastStatus_ = SdCopyDisplayStatus{};
  activeScreen_ = Screen::SdCopyConfirm;
}

void SystemUI::showSdCopyProgress_() {
  sdCopyDirty_ = true;
  sdCopyStatusText_.clear();
  sdCopyStatusUntil_ = 0;
  sdCopyStatusColor_ = TFT_WHITE;
  sdCopyPendingExit_ = false;
  sdCopyLastStatus_ = SdCopyDisplayStatus{};
  sdCopyProgressNeedsClear_ = true;
  sdCopyHeader_.clear();
  sdCopyBarFill_ = 0;
  sdCopyHelperText_.clear();
  sdCopyHelperColor_ = TFT_WHITE;
  activeScreen_ = Screen::SdCopyProgress;
}

bool SystemUI::handleSdCopyConfirmButtons_(uint8_t index, BtnEvent e) {
  if (index == 1) {
    if (e == BtnEvent::Single) {
      sdCopyCancel_();
      showSetup();
      setupMenu_.showStatus(i18n.t("slideshow.aborted"), 1000);
      return true;
    }
    if (e == BtnEvent::Double) {
      hide();
      return true;
    }
    return false;
  }

  if (index != 2) {
    return false;
  }

  switch (e) {
    case BtnEvent::Single:
      sdCopySelection_ ^= 1;
      sdCopyDirty_ = true;
      return true;
    case BtnEvent::Long: {
      if (sdCopySelection_ == 0) {
        sdCopyCancel_();
        showSetup();
        setupMenu_.showStatus(i18n.t("slideshow.aborted"), 1000);
        return true;
      }
      if (!sdCopyBegin_()) {
        showSdCopyStatus_(i18n.t("system.copy_running"), 1500, TFT_RED);
        return true;
      }
      showSdCopyProgress_();
      return true;
    }
    default:
      break;
  }
  return false;
}

bool SystemUI::handleSdCopyProgressButtons_(uint8_t index, BtnEvent e) {
  if (index != 2) {
    return false;
  }
  if (e == BtnEvent::Long) {
    sdCopyAbort_();
    showSdCopyStatus_(i18n.t("system.abort_requested"), 1500, TFT_WHITE);
    return true;
  }
  return false;
}

void SystemUI::drawSdCopyConfirm_() {
  uint32_t now = millis();
  if (!sdCopyStatusText_.isEmpty() && now >= sdCopyStatusUntil_) {
    sdCopyStatusText_.clear();
    sdCopyStatusUntil_ = 0;
    sdCopyDirty_ = true;
  }

  if (!sdCopyDirty_) {
    return;
  }
  sdCopyDirty_ = false;

  tft.fillScreen(TFT_BLACK);
  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  const int16_t top = 24;
  TextRenderer::drawCentered(top, i18n.t("system.sd_copy"), TFT_WHITE, TFT_BLACK);

  const char* labelKeys[2] = {"system.no", "system.yes"};
  for (uint8_t i = 0; i < 2; ++i) {
    int16_t y = top + line + spacing + static_cast<int16_t>(i) * (line + spacing);
    bool selected = (sdCopySelection_ == i);
    uint16_t color = selected ? TFT_WHITE : TFT_DARKGREY;
    String text = String(i18n.t(labelKeys[i]));
    if (selected) {
      text = String("> ") + text;
    }
    TextRenderer::drawCentered(y, text, color, TFT_BLACK);
  }

  const int16_t helperLine = TextRenderer::helperLineHeight();
  const int16_t hintY = TFT_H - (helperLine * 2) - 32;
  if (!sdCopyStatusText_.isEmpty()) {
    TextRenderer::drawHelperCentered(hintY, sdCopyStatusText_, sdCopyStatusColor_, TFT_BLACK);
  } else {
    TextRenderer::drawHelperCentered(hintY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
    TextRenderer::drawHelperCentered(hintY + helperLine + 2, i18n.t("buttons.long_confirm"), TFT_WHITE, TFT_BLACK);
  }
}

void SystemUI::drawSdCopyProgress_() {
  sdCopyTick_();

  SdCopyDisplayStatus status = sdCopyStatus_();
  if (status.filesProcessed != sdCopyLastStatus_.filesProcessed ||
      status.fileCount != sdCopyLastStatus_.fileCount ||
      status.bytesDone != sdCopyLastStatus_.bytesDone ||
      status.bytesTotal != sdCopyLastStatus_.bytesTotal ||
      status.running != sdCopyLastStatus_.running) {
    sdCopyLastStatus_ = status;
    sdCopyDirty_ = true;
  }

  uint32_t now = millis();
  if (!sdCopyStatusText_.isEmpty() && now >= sdCopyStatusUntil_) {
    sdCopyStatusText_.clear();
    sdCopyStatusUntil_ = 0;
    sdCopyDirty_ = true;
  }

  if (sdCopyOutcome_ != SdCopyOutcome::None) {
    bool shouldExitToSetup =
        (sdCopyOutcome_ == SdCopyOutcome::Success || sdCopyOutcome_ == SdCopyOutcome::Aborted);
    uint16_t color = (sdCopyOutcome_ == SdCopyOutcome::Error) ? TFT_RED : TFT_WHITE;
    uint32_t duration = (sdCopyOutcome_ == SdCopyOutcome::Error) ? 1800 : 1500;
    String message = sdCopyOutcomeMessage_;
    if (message.isEmpty()) {
      switch (sdCopyOutcome_) {
        case SdCopyOutcome::Success:
          message = String(i18n.t("system.done"));
          break;
        case SdCopyOutcome::Error:
          message = String(i18n.t("system.error"));
          break;
        case SdCopyOutcome::Aborted:
          message = String(i18n.t("slideshow.aborted"));
          break;
        default:
          break;
      }
    }
    if (shouldExitToSetup) {
      sdCopyOutcome_ = SdCopyOutcome::None;
      sdCopyOutcomeMessage_.clear();
      sdCopyPendingExit_ = false;
      sdCopyStatusText_.clear();
      sdCopyStatusUntil_ = 0;
      showSetup();
      setupMenu_.showStatus(message, duration);
      return;
    } else {
      showSdCopyStatus_(message, duration, color);
      sdCopyPendingExit_ = true;
      sdCopyOutcome_ = SdCopyOutcome::None;
      sdCopyOutcomeMessage_.clear();
    }
  }

  if (sdCopyPendingExit_ && sdCopyStatusUntil_ && now >= sdCopyStatusUntil_) {
    sdCopyPendingExit_ = false;
    showSetup();
    return;
  }

  if (!sdCopyDirty_ && !sdCopyProgressNeedsClear_) {
    return;
  }
  sdCopyDirty_ = false;

  const int16_t line = TextRenderer::lineHeight();
  const int16_t top = 40;
  const int16_t barWidth = TFT_W - 40;
  const int16_t barHeight = 20;
  const int16_t barX = (TFT_W - barWidth) / 2;
  const int16_t barY = top + line + 16;
  const int16_t helperY = barY + barHeight + 14;
  const int16_t helperHeight = TextRenderer::helperLineHeight() * 2 + 24;

  if (sdCopyProgressNeedsClear_) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
    sdCopyProgressNeedsClear_ = false;
  }

  String header = (status.fileCount > 0 && status.running)
                      ? i18n.t("system.copying", String(status.filesProcessed), String(status.fileCount))
                      : String(i18n.t("system.preparing"));
  if (header != sdCopyHeader_) {
    sdCopyHeader_ = header;
    tft.fillRect(0, top - 6, TFT_W, line + 12, TFT_BLACK);
    TextRenderer::drawCentered(top, sdCopyHeader_, TFT_WHITE, TFT_BLACK);
  }

  float progress = 0.0f;
  if (status.bytesTotal > 0) {
    progress = static_cast<float>(status.bytesDone) / static_cast<float>(status.bytesTotal);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
  }
  int16_t fill = static_cast<int16_t>((barWidth - 2) * progress);
  if (!status.running) {
    fill = 0;
  }
  if (fill != sdCopyBarFill_) {
    sdCopyBarFill_ = fill;
    tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, TFT_BLACK);
    if (fill > 0) {
      tft.fillRect(barX + 1, barY + 1, fill, barHeight - 2, TFT_WHITE);
    }
  }

  String helperText = sdCopyStatusText_.isEmpty() ? String(i18n.t("buttons.long_abort")) : sdCopyStatusText_;
  uint16_t helperColor = sdCopyStatusText_.isEmpty() ? TFT_WHITE : sdCopyStatusColor_;
  if (helperText != sdCopyHelperText_ || helperColor != sdCopyHelperColor_) {
    sdCopyHelperText_ = helperText;
    sdCopyHelperColor_ = helperColor;
    tft.fillRect(0, helperY - 6, TFT_W, helperHeight, TFT_BLACK);
    TextRenderer::drawHelperCentered(helperY, sdCopyHelperText_, sdCopyHelperColor_, TFT_BLACK);
  }
}

void SystemUI::showSdCopyStatus_(const String& text, uint32_t duration_ms, uint16_t color) {
  sdCopyStatusText_ = text;
  sdCopyStatusUntil_ = millis() + duration_ms;
  sdCopyStatusColor_ = color;
  sdCopyDirty_ = true;
}

void SystemUI::resetSdCopyUi_() {
  sdCopySelection_ = 1;
  sdCopyDirty_ = true;
  sdCopyStatusText_.clear();
  sdCopyStatusUntil_ = 0;
  sdCopyStatusColor_ = TFT_WHITE;
  sdCopyPendingExit_ = false;
  sdCopyLastStatus_ = SdCopyDisplayStatus{};
  sdCopyState_ = SdCopyState::Idle;
  sdCopyOutcome_ = SdCopyOutcome::None;
  sdCopyOutcomeMessage_.clear();
  sdCopyAbortRequest_ = false;
  sdCopyCloseFiles_();
  sdCopyQueue_.clear();
  sdCopyQueueIndex_ = 0;
  sdCopyBytesTotal_ = 0;
  sdCopyBytesDone_ = 0;
  sdCopyFileBytesDone_ = 0;
  sdCopyProgressNeedsClear_ = true;
  sdCopyHeader_.clear();
  sdCopyBarFill_ = 0;
  sdCopyHelperText_.clear();
  sdCopyHelperColor_ = TFT_WHITE;
}

bool SystemUI::sdCopyStartConfirm_() {
  if (sdCopyState_ == SdCopyState::Running) {
    return false;
  }
  if (sdCopyState_ != SdCopyState::Confirm) {
    sdCopyState_ = SdCopyState::Confirm;
    sdCopyAbortRequest_ = false;
    sdCopyResetResult_();
    sdCopyQueue_.clear();
    sdCopyQueueIndex_ = 0;
    sdCopyBytesTotal_ = 0;
    sdCopyBytesDone_ = 0;
    sdCopyFileBytesDone_ = 0;
    sdCopyCloseFiles_();
  }
  return true;
}

bool SystemUI::sdCopyBegin_() {
  if (sdCopyState_ == SdCopyState::Running) {
    return true;
  }
  if (sdCopyState_ != SdCopyState::Confirm) {
    return false;
  }
  sdCopyResetResult_();
  if (!sdCopyPrepareQueue_()) {
    sdCopyFinalize_(SdCopyOutcome::Error, i18n.t("system.no_files_found"));
    return false;
  }

  if (!LittleFS.exists("/system")) {
    LittleFS.mkdir("/system");
  }
  if (!LittleFS.exists("/system/fonts")) {
    LittleFS.mkdir("/system/fonts");
  }

  sdCopyState_ = SdCopyState::Running;
  sdCopyAbortRequest_ = false;
  sdCopyDirty_ = true;
  return true;
}

void SystemUI::sdCopyCancel_() {
  if (sdCopyState_ == SdCopyState::Confirm) {
    sdCopyState_ = SdCopyState::Idle;
    sdCopyAbortRequest_ = false;
    sdCopyQueue_.clear();
    sdCopyCloseFiles_();
  }
}

void SystemUI::sdCopyAbort_() {
  if (sdCopyState_ == SdCopyState::Running) {
    sdCopyAbortRequest_ = true;
    sdCopyTick_();
    if (sdCopyState_ == SdCopyState::Running) {
      sdCopyFinalize_(SdCopyOutcome::Aborted, i18n.t("slideshow.aborted"));
    }
  }
}

void SystemUI::sdCopyTick_() {
  if (sdCopyState_ != SdCopyState::Running) {
    return;
  }
  const uint32_t start = millis();
  while (sdCopyState_ == SdCopyState::Running && (millis() - start) < kSdCopyTickBudgetMs) {
    if (sdCopyAbortRequest_) {
      sdCopyFinalize_(SdCopyOutcome::Aborted, i18n.t("slideshow.aborted"));
      return;
    }

    if (!sdCopySrc_) {
      if (sdCopyQueueIndex_ >= sdCopyQueue_.size()) {
        sdCopyFinalize_(SdCopyOutcome::Success, "");
        return;
      }

      const CopyItem& item = sdCopyQueue_[sdCopyQueueIndex_];
      sdCopySrc_ = SD.open(item.path.c_str(), FILE_READ);
      sdCopyDst_ = LittleFS.open(item.destPath.c_str(), FILE_WRITE);
      sdCopyFileBytesDone_ = 0;

      if (!sdCopySrc_ || !sdCopyDst_) {
        sdCopyFinalize_(SdCopyOutcome::Error, i18n.t("system.error_at", item.name));
        return;
      }
    }

    const size_t chunkSize = sdCopyBuffer_.size();
    size_t available = sdCopySrc_.available();
    if (available == 0) {
      sdCopySrc_.close();
      sdCopyDst_.close();
      ++sdCopyQueueIndex_;
      continue;
    }

    size_t toRead = std::min(chunkSize, available);
    size_t n = sdCopySrc_.read(sdCopyBuffer_.data(), toRead);
    if (n == 0) {
      sdCopyFinalize_(SdCopyOutcome::Error, i18n.t("system.read_error"));
      return;
    }
    if (sdCopyDst_.write(sdCopyBuffer_.data(), n) != n) {
      sdCopyFinalize_(SdCopyOutcome::Error, i18n.t("system.write_error"));
      return;
    }

    sdCopyBytesDone_ += n;
    sdCopyFileBytesDone_ += n;
  }
}

void SystemUI::sdCopyResetResult_() {
  sdCopyOutcome_ = SdCopyOutcome::None;
  sdCopyOutcomeMessage_.clear();
}

void SystemUI::sdCopyFinalize_(SdCopyOutcome outcome, const String& message) {
  sdCopyCloseFiles_();
  sdCopyQueue_.clear();
  sdCopyQueueIndex_ = 0;
  sdCopyBytesTotal_ = 0;
  sdCopyBytesDone_ = 0;
  sdCopyFileBytesDone_ = 0;
  sdCopyAbortRequest_ = false;
  sdCopyState_ = SdCopyState::Idle;
  sdCopyDirty_ = true;

  sdCopyOutcome_ = outcome;
  sdCopyOutcomeMessage_ = message;

  if (outcome == SdCopyOutcome::Success && callbacks_.setSource) {
    callbacks_.setSource(SlideSource::Flash);
  }
}

bool SystemUI::sdCopyPrepareQueue_() {
  sdCopyQueue_.clear();
  sdCopyQueueIndex_ = 0;
  sdCopyBytesDone_ = 0;
  sdCopyBytesTotal_ = 0;
  sdCopyFileBytesDone_ = 0;
  sdCopyAbortRequest_ = false;
  sdCopyCloseFiles_();

  if (!sdCopyEnsureSdReady_()) {
    return false;
  }
  if (!sdCopyEnsureFlashReady_()) {
    return false;
  }
  if (!ensureDirectory("/scripts")) {
    return false;
  }

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
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

    if (lower == "bootlogo.jpg" || lower == "boot.jpg") {
      ci.type = CopyFileType::Bootlogo;
      ci.destPath = "/system/bootlogo.jpg";
      accepted = true;
    } else if (lower == "textapp.cfg") {
      if (!LittleFS.exists("/textapp.cfg")) {
        ci.type = CopyFileType::Config;
        ci.destPath = "/textapp.cfg";
        accepted = true;
      }
    } else if (lower == "i18n.json") {
      ci.type = CopyFileType::Config;
      ci.destPath = "/system/i18n.json";
      accepted = true;
    } else if (lower == "font.vlw" || lower == "fontsmall.vlw") {
      if (size <= 18384) {
        ci.type = CopyFileType::Font;
        ci.destPath = (lower == "fontsmall.vlw") ? "/system/fontsmall.vlw" : "/system/font.vlw";
        accepted = true;
      }
    } else if (lower.endsWith(".vlw")) {
      if (size <= 30720) {
        ci.type = CopyFileType::Font;
        ci.destPath = String("/system/fonts/") + lower;
        accepted = true;
      }
    } else if (lower.endsWith(".lua")) {
      ci.type = CopyFileType::Lua;
      ci.destPath = String("/scripts/") + filename;
      accepted = true;
    } else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
      ci.type = CopyFileType::Jpg;
      String destName = filename;
      String ext = ".jpg";
      int dot = destName.lastIndexOf('.');
      String base = destName;
      if (dot >= 0) {
        ext = destName.substring(dot);
        base = destName.substring(0, dot);
      }
      ci.destPath = String(kFlashSlidesDir) + "/" + destName;
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
    return false;
  }

  std::sort(items.begin(), items.end(), [](const CopyItem& a, const CopyItem& b) {
    if (a.type != b.type) {
      return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
    }
    return a.name < b.name;
  });

  sdCopyQueue_ = std::move(items);
  for (const auto& item : sdCopyQueue_) {
    sdCopyBytesTotal_ += item.size;
  }
  return true;
}

bool SystemUI::sdCopyEnsureFlashReady_() {
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

bool SystemUI::sdCopyEnsureSdReady_() {
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

void SystemUI::sdCopyCloseFiles_() {
  if (sdCopySrc_) sdCopySrc_.close();
  if (sdCopyDst_) sdCopyDst_.close();
}

SystemUI::SdCopyDisplayStatus SystemUI::sdCopyStatus_() const {
  SdCopyDisplayStatus status;
  status.running = (sdCopyState_ == SdCopyState::Running);
  status.fileCount = sdCopyQueue_.size();
  size_t processed = sdCopyQueueIndex_;
  if (sdCopyState_ == SdCopyState::Running && processed < status.fileCount) {
    processed += 1;
  }
  status.filesProcessed = std::min(processed, status.fileCount);
  status.bytesDone = sdCopyBytesDone_;
  status.bytesTotal = sdCopyBytesTotal_;
  return status;
}

bool SystemUI::showTransferScreen_() {
  if (!callbacks_.ensureSlideshowActive || !callbacks_.ensureSlideshowActive()) {
    return false;
  }
  if (activeScreen_ != Screen::Transfer) {
    resetTransferUi_();
    enableTransfer_(true);
    transferFooter_ = String(i18n.t("buttons.btn1_actions"));
    activeScreen_ = Screen::Transfer;
  }
  return true;
}

void SystemUI::resetTransferUi_() {
  enableTransfer_(false);
  transferDirty_ = true;
  transferNeedsClear_ = true;
  transferSource_ = TransferSource::None;
  transferState_ = TransferState::Idle;
  transferFilename_.clear();
  transferMessage_.clear();
  transferBytesExpected_ = 0;
  transferBytesReceived_ = 0;
  transferBarFill_ = 0;
  transferProgressFrameDrawn_ = false;
  transferHeader_.clear();
  transferHeaderSub_.clear();
  transferPrimary_.clear();
  transferSecondary_.clear();
  transferFooter_.clear();
  transferStatusText_.clear();
  transferStatusUntil_ = 0;
  transferStatusColor_ = TFT_WHITE;
}

bool SystemUI::handleTransferButtons_(uint8_t index, BtnEvent e) {
  if (index == 1) {
    if (e == BtnEvent::Single) {
      showSetup();
      return true;
    }
    if (e == BtnEvent::Double) {
      hide();
      return true;
    }
    return false;
  }
  if (index == 2 && e == BtnEvent::Long) {
    hide();
    return true;
  }
  return false;
}

void SystemUI::onTransferStarted(TransferSource src, const char* filename, size_t size) {
  if (!isTransferActive()) {
    return;
  }
  transferSource_ = src;
  transferState_ = TransferState::Receiving;
  transferFilename_ = filename ? String(filename) : String();
  transferMessage_.clear();
  transferBytesExpected_ = size;
  transferBytesReceived_ = 0;
  transferDirty_ = true;
}

void SystemUI::onTransferCompleted(TransferSource src, const char* filename, size_t size) {
  if (!isTransferActive()) {
    return;
  }
  bool focusOk = callbacks_.focusTransferredFile
                     ? callbacks_.focusTransferredFile(filename ? filename : "", size)
                     : true;
  if (!focusOk) {
    onTransferError(src, i18n.t("transfer.flash_update_failed"));
    return;
  }
  transferSource_ = src;
  transferState_ = TransferState::Completed;
  transferFilename_ = filename ? String(filename) : String();
  transferBytesExpected_ = size;
  transferBytesReceived_ = size;
  transferDirty_ = true;
  showTransferStatus_(i18n.t("transfer.reception_completed"), 1500, TFT_WHITE);
}

void SystemUI::onTransferError(TransferSource src, const char* message) {
  if (!isTransferActive()) {
    return;
  }
  transferSource_ = src;
  transferState_ = TransferState::Error;
  transferMessage_ = message ? String(message) : String(i18n.t("transfer.failed"));
  transferDirty_ = true;
  showTransferStatus_(transferMessage_, 1800, TFT_RED);
}

void SystemUI::onTransferAborted(TransferSource src, const char* message) {
  if (!isTransferActive()) {
    return;
  }
  transferSource_ = src;
  transferState_ = TransferState::Aborted;
  transferMessage_ = message ? String(message) : String(i18n.t("transfer.aborted"));
  transferBytesReceived_ = 0;
  transferDirty_ = true;
  showTransferStatus_(transferMessage_, 1500, TFT_WHITE);
}

void SystemUI::enableTransfer_(bool enable) {
  if (transferEnabled_ == enable) {
    return;
  }
  transferEnabled_ = enable;
  BleImageTransfer::setTransferEnabled(enable);
  SerialImageTransfer::setTransferEnabled(enable);
}

void SystemUI::updateTransferProgress_() {
  if (transferState_ != TransferState::Receiving) {
    transferBytesReceived_ = 0;
    transferBytesExpected_ = (transferState_ == TransferState::Completed) ? transferBytesExpected_ : 0;
    return;
  }

  size_t expected = 0;
  size_t received = 0;
  if (transferSource_ == TransferSource::Usb) {
    expected = SerialImageTransfer::bytesExpected();
    received = SerialImageTransfer::bytesReceived();
  } else {
    expected = BleImageTransfer::bytesExpected();
    received = BleImageTransfer::bytesReceived();
  }
  if (expected > 0 && expected != transferBytesExpected_) {
    transferBytesExpected_ = expected;
    transferDirty_ = true;
  }
  if (received != transferBytesReceived_) {
    transferBytesReceived_ = received;
    transferDirty_ = true;
  }
}

const char* SystemUI::transferSourceLabel_(TransferSource src) const {
  switch (src) {
    case TransferSource::Usb:
      return i18n.t("transfer.usb");
    case TransferSource::Ble:
      return i18n.t("transfer.bluetooth");
    default:
      return i18n.t("transfer.transfer");
  }
}

String SystemUI::formatKilobytes_(size_t bytes) const {
  if (bytes == 0) return String("0");
  uint32_t whole = bytes / 1024;
  uint32_t tenth = static_cast<uint32_t>(((bytes % 1024) * 10UL) / 1024UL);
  if (whole == 0 && tenth == 0) {
    return String("0.1");
  }
  if (tenth == 0) {
    return String(whole);
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u", whole, tenth);
  return String(buf);
}

void SystemUI::showTransferStatus_(const String& text, uint32_t duration_ms, uint16_t color) {
  transferStatusText_ = text;
  transferStatusUntil_ = millis() + duration_ms;
  transferStatusColor_ = color;
  transferDirty_ = true;
}

void SystemUI::drawTransfer_() {
  updateTransferProgress_();

  uint32_t now = millis();
  if (!transferStatusText_.isEmpty() && now >= transferStatusUntil_) {
    transferStatusText_.clear();
    transferStatusUntil_ = 0;
    transferDirty_ = true;
  }

  if (!transferDirty_ && !transferNeedsClear_) {
    return;
  }
  transferDirty_ = false;

  const int16_t line = TextRenderer::lineHeight();
  const int16_t headerY = 28;
  const int16_t barWidth = TFT_W - 48;
  const int16_t barHeight = 16;
  const int16_t barX = (TFT_W - barWidth) / 2;
  int16_t primaryY = headerY + line + 10;
  int16_t secondaryY = primaryY + line + 8;
  int16_t barY = secondaryY + line + 18;
  int16_t statusY = barY + barHeight + 16;
  const int16_t footerY = TFT_H - line - 24;

  String header;
  String headerSub;
  String headerTertiary;
  if (transferState_ == TransferState::Receiving) {
    header = transferSourceLabel_(transferSource_);
    headerSub = String(i18n.t("transfer.receiving"));
    headerTertiary = transferFilename_.isEmpty() ? String() : transferFilename_;
  } else if (transferState_ == TransferState::Completed) {
    header = transferSourceLabel_(transferSource_);
    headerSub = String(i18n.t("transfer.completed"));
    headerTertiary = transferFilename_.isEmpty() ? String(i18n.t("transfer.file_saved")) : transferFilename_;
  } else if (transferState_ == TransferState::Error) {
    header = transferSourceLabel_(transferSource_);
    headerSub = String(i18n.t("transfer.failed"));
    headerTertiary = transferMessage_;
  } else if (transferState_ == TransferState::Aborted) {
    header = transferSourceLabel_(transferSource_);
    headerSub = String(i18n.t("transfer.aborted"));
    headerTertiary = transferMessage_;
  } else {
    header = String(i18n.t("transfer.usb_ble"));
    headerSub = String(i18n.t("transfer.transmission"));
    headerTertiary = String(i18n.t("transfer.start_at_host"));
  }
  int headerLines = 0;
  if (!header.isEmpty()) headerLines++;
  if (!headerSub.isEmpty()) headerLines++;
  if (!headerTertiary.isEmpty()) headerLines++;

  if (transferNeedsClear_) {
    tft.fillScreen(TFT_BLACK);
    transferNeedsClear_ = false;
    transferProgressFrameDrawn_ = false;
    transferBarFill_ = 0;
    transferHeader_.clear();
    transferHeaderSub_.clear();
    transferHeaderTertiary_.clear();
    transferPrimary_.clear();
    transferSecondary_.clear();
    transferFooter_.clear();
  }

  if (header != transferHeader_ || headerSub != transferHeaderSub_ || headerTertiary != transferHeaderTertiary_) {
    int16_t blockHeight = headerLines ? headerLines * (line + 6) + 8 : line + 12;
    tft.fillRect(0, headerY - 6, TFT_W, blockHeight, TFT_BLACK);
    int16_t yCursor = headerY;
    auto drawHeaderLine = [&](const String& text) {
      if (text.isEmpty()) return;
      TextRenderer::drawCentered(yCursor, text, TFT_WHITE, TFT_BLACK);
      yCursor += line + 6;
    };
    drawHeaderLine(header);
    drawHeaderLine(headerSub);
    drawHeaderLine(headerTertiary);
    transferHeader_ = header;
    transferHeaderSub_ = headerSub;
    transferHeaderTertiary_ = headerTertiary;
  }

  int16_t headerBlock = headerLines * (line + 6);
  primaryY = headerY + headerBlock + 6;
  secondaryY = primaryY + line + 8;
  barY = secondaryY + line + 18;
  statusY = barY + barHeight + 16;

  String primary;
  String secondary;
  switch (transferState_) {
    case TransferState::Idle:
      primary.clear();
      secondary.clear();
      break;
    case TransferState::Receiving: {
      primary = String(transferSourceLabel_(transferSource_)) + String(": Empfang läuft");
      if (transferBytesExpected_ > 0) {
        uint32_t pct = static_cast<uint32_t>(
            (transferBytesReceived_ * 100UL) / transferBytesExpected_);
        String recv = formatKilobytes_(transferBytesReceived_);
        String total = formatKilobytes_(transferBytesExpected_);
        secondary = String(pct) + String("% (") + recv + String("/") + total + String(" KB)");
      } else {
        secondary = String(transferBytesReceived_) + String(" B erhalten");
      }
      break;
    }
    case TransferState::Completed:
      primary = String(i18n.t("transfer.completed"));
      secondary = transferFilename_.isEmpty() ? String(i18n.t("transfer.file_saved")) : String();
      break;
    case TransferState::Error:
      primary = String(i18n.t("transfer.failed"));
      secondary.clear();
      break;
    case TransferState::Aborted:
      primary = String(i18n.t("transfer.aborted"));
      secondary.clear();
      break;
  }

  if (primary != transferPrimary_) {
    tft.fillRect(0, primaryY - 6, TFT_W, line + 12, TFT_BLACK);
    TextRenderer::drawCentered(primaryY, primary, TFT_WHITE, TFT_BLACK);
    transferPrimary_ = primary;
  }
  if (secondary != transferSecondary_) {
    tft.fillRect(0, secondaryY - 6, TFT_W, line + 12, TFT_BLACK);
    if (!secondary.isEmpty()) {
      TextRenderer::drawCentered(secondaryY, secondary, TFT_WHITE, TFT_BLACK);
    }
    transferSecondary_ = secondary;
  }

  if (transferState_ == TransferState::Receiving && transferBytesExpected_ > 0) {
    uint16_t fill = static_cast<uint16_t>(
        ((barWidth - 2) * transferBytesReceived_) /
        (transferBytesExpected_ ? transferBytesExpected_ : 1));
    if (!transferProgressFrameDrawn_) {
      tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
      transferProgressFrameDrawn_ = true;
      transferBarFill_ = 0;
    }
    if (fill != transferBarFill_) {
      if (fill > barWidth - 2) fill = barWidth - 2;
      tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, TFT_BLACK);
      if (fill > 0) {
        tft.fillRect(barX + 1, barY + 1, fill, barHeight - 2, TFT_WHITE);
      }
      transferBarFill_ = fill;
    }
  } else if (transferProgressFrameDrawn_) {
    tft.fillRect(barX, barY, barWidth, barHeight, TFT_BLACK);
    transferProgressFrameDrawn_ = false;
    transferBarFill_ = 0;
  }

  if (!transferStatusText_.isEmpty()) {
    tft.fillRect(0, statusY - 6, TFT_W, line + 12, TFT_BLACK);
    TextRenderer::drawCentered(statusY, transferStatusText_, transferStatusColor_, TFT_BLACK);
  } else if (transferState_ == TransferState::Idle) {
    tft.fillRect(0, statusY - 6, TFT_W, line + 12, TFT_BLACK);
    TextRenderer::drawCentered(statusY, i18n.t("transfer.start_transfer"), TFT_DARKGREY, TFT_BLACK);
  }

  if (!transferFooter_.isEmpty()) {
    tft.fillRect(0, footerY - 6, TFT_W, line + 12, TFT_BLACK);
    TextRenderer::drawCentered(footerY, transferFooter_, TFT_WHITE, TFT_BLACK);
  }
}

// ============================================================================
// Language Selection
// ============================================================================

void SystemUI::showLanguageSelection_() {
  languageSelection_ = i18n.currentLanguageIndex();
  languageDirty_ = true;
  activeScreen_ = Screen::Language;
}

bool SystemUI::handleLanguageButtons_(uint8_t index, BtnEvent e) {
  if (index == 1) {
    // BTN1: Exit back to Setup
    if (e == BtnEvent::Single || e == BtnEvent::Double) {
      showSetup();
      setupMenu_.draw(true);  // Force redraw with new language
      return true;
    }
    return false;
  }

  if (index != 2) {
    return false;
  }

  switch (e) {
    case BtnEvent::Single: {
      // Cycle through languages
      uint8_t langCount = i18n.languageCount();
      languageSelection_ = (languageSelection_ + 1) % langCount;
      languageDirty_ = true;
      return true;
    }
    case BtnEvent::Long: {
      // Confirm language selection
      const char* const* langs = i18n.availableLanguages();
      if (langs[languageSelection_]) {
        i18n.setLanguage(langs[languageSelection_]);
        // Redraw to show new language
        languageDirty_ = true;
      }
      return true;
    }
    case BtnEvent::Double:
      // Exit back to Setup
      showSetup();
      setupMenu_.draw(true);  // Force redraw with new language
      return true;
    default:
      break;
  }

  return false;
}

void SystemUI::drawLanguageSelection_() {
  if (!languageDirty_) return;
  languageDirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  int16_t top = 24;

  // Title
  TextRenderer::drawCentered(top, i18n.t("language.select"), TFT_WHITE, TFT_BLACK);

  // Language options
  const char* const* langs = i18n.availableLanguages();
  uint8_t langCount = i18n.languageCount();

  const char* langNameKeys[] = {
    "language.german",
    "language.english",
    "language.french",
    "language.italian"
  };

  for (uint8_t i = 0; i < langCount && i < 4; ++i) {
    int16_t y = top + line + spacing + static_cast<int16_t>(i) * (line + spacing);
    String label = String(i18n.t(langNameKeys[i]));
    uint16_t color = TFT_DARKGREY;

    if (i == languageSelection_) {
      label = String("> ") + label;
      color = TFT_WHITE;
    }

    // Show current language indicator
    if (i == i18n.currentLanguageIndex()) {
      label += " *";
    }

    TextRenderer::drawCentered(y, label, color, TFT_BLACK);
  }

  // Button hints at bottom
  const int16_t helperLine = TextRenderer::helperLineHeight();
  const int16_t hintY = TFT_H - (helperLine * 2) - 8 - 20;
  TextRenderer::drawHelperCentered(hintY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(hintY + helperLine + 2,
                                  i18n.t("buttons.long_confirm"), TFT_WHITE, TFT_BLACK);
}
