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

void SystemUI::showSetup(App* activeApp) {
  resetSdCopyUi_();
  resetTransferUi_();

  // IMPORTANT: Ensure system font is loaded before showing menu
  // Apps like TextApp may have loaded their own fonts
  TextRenderer::ensureLoaded();

  activeScreen_ = Screen::Setup;
  activeApp_ = activeApp;
  setupMenu_.show();
}

void SystemUI::hide() {
  if (activeScreen_ == Screen::Setup) {
    setupMenu_.hide();
    // Clear screen and resume app to redraw after setup menu
    if (activeApp_) {
      tft.fillScreen(TFT_BLACK);
      activeApp_->resume();
    }
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
  activeApp_ = nullptr;
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

  bool shouldHide = true;
  switch (item) {
    case SetupMenu::Item::Language:
      shouldHide = false;
      showLanguageSelection_();
      break;
    case SetupMenu::Item::UsbBleTransfer: {
      shouldHide = false;
      // Transfer screen works independently - no need to activate slideshow
      // Slideshow will be updated via focusTransferredFile() callback after transfer
      if (!showTransferScreen_()) {
        setupMenu_.showStatus(i18n.t("system.transfer_impossible"), 1500);
      }
      break;
    }
    case SetupMenu::Item::SdTransfer: {
      shouldHide = false;
      // SD copy works independently - no need to activate slideshow
      // Slideshow will pick up new files when it's next activated
      if (!sdCopyStartConfirm_()) {
        setupMenu_.showStatus(i18n.t("system.copy_running"), 1500);
        break;
      }
      showSdCopyConfirm_();
      break;
    }
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

  // Ensure system font is loaded
  TextRenderer::ensureLoaded();

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

  // Ensure system font is loaded
  TextRenderer::ensureLoaded();

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
  // Split title into two lines: "SD-" and "Transfer"
  TextRenderer::drawCentered(top, "SD-", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(top + line, i18n.t("transfer.transfer"), TFT_WHITE, TFT_BLACK);

  const char* labelKeys[2] = {"system.no", "system.yes"};
  for (uint8_t i = 0; i < 2; ++i) {
    int16_t y = top + line + line + spacing + 15 + static_cast<int16_t>(i) * (line + spacing);
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

  // Handle copy completion from engine
  if (copyEngine_.getState() == SDCopyEngine::State::Finished) {
    auto outcome = copyEngine_.getOutcome();
    if (outcome != SDCopyEngine::Outcome::None) {
      bool shouldExitToSetup =
          (outcome == SDCopyEngine::Outcome::Success || outcome == SDCopyEngine::Outcome::Aborted);
      uint16_t color = (outcome == SDCopyEngine::Outcome::Error) ? TFT_RED : TFT_WHITE;
      uint32_t duration = (outcome == SDCopyEngine::Outcome::Error) ? 1800 : 1500;
      String message = copyEngine_.getErrorMessage();
      if (message.isEmpty()) {
        switch (outcome) {
          case SDCopyEngine::Outcome::Success:
            message = String(i18n.t("system.done"));
            break;
          case SDCopyEngine::Outcome::Error:
            message = String(i18n.t("system.error"));
            break;
          case SDCopyEngine::Outcome::Aborted:
            message = String(i18n.t("slideshow.aborted"));
            break;
          default:
            break;
        }
      }
      if (shouldExitToSetup) {
        copyEngine_.reset();
        sdCopyPendingExit_ = false;
        sdCopyStatusText_.clear();
        sdCopyStatusUntil_ = 0;
        showSetup();
        setupMenu_.showStatus(message, duration);
        return;
      } else {
        showSdCopyStatus_(message, duration, color);
        sdCopyPendingExit_ = true;
        // Don't reset engine here - will be reset when exiting
      }
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
  sdCopyUiState_ = SdCopyUiState::Idle;
  copyEngine_.reset();
  sdCopyProgressNeedsClear_ = true;
  sdCopyHeader_.clear();
  sdCopyBarFill_ = 0;
  sdCopyHelperText_.clear();
  sdCopyHelperColor_ = TFT_WHITE;
}

bool SystemUI::sdCopyStartConfirm_() {
  if (sdCopyUiState_ == SdCopyUiState::Running) {
    return false;
  }
  if (sdCopyUiState_ != SdCopyUiState::Confirm) {
    sdCopyUiState_ = SdCopyUiState::Confirm;
    sdCopyResetResult_();
    copyEngine_.reset();
  }
  return true;
}

bool SystemUI::sdCopyBegin_() {
  if (sdCopyUiState_ == SdCopyUiState::Running) {
    return true;
  }
  if (sdCopyUiState_ != SdCopyUiState::Confirm) {
    return false;
  }

  sdCopyResetResult_();

  // Prepare engine (will create required directories)
  if (!copyEngine_.prepare()) {
    // Engine sets outcome/error message internally
    sdCopyUiState_ = SdCopyUiState::Idle;
    return false;
  }

  sdCopyUiState_ = SdCopyUiState::Running;
  sdCopyDirty_ = true;
  return true;
}

void SystemUI::sdCopyCancel_() {
  if (sdCopyUiState_ == SdCopyUiState::Confirm) {
    sdCopyUiState_ = SdCopyUiState::Idle;
    copyEngine_.reset();
  }
}

void SystemUI::sdCopyAbort_() {
  if (sdCopyUiState_ == SdCopyUiState::Running) {
    copyEngine_.abort();
  }
}

void SystemUI::sdCopyTick_() {
  if (sdCopyUiState_ != SdCopyUiState::Running) {
    return;
  }

  // Run engine tick
  copyEngine_.tick();

  // Check if finished
  if (copyEngine_.getState() == SDCopyEngine::State::Finished) {
    sdCopyUiState_ = SdCopyUiState::Idle;
    sdCopyDirty_ = true;

    // Handle outcome
    if (copyEngine_.getOutcome() == SDCopyEngine::Outcome::Success) {
      if (callbacks_.setSource) {
        callbacks_.setSource(SlideSource::Flash);
      }
    }
  }
}

void SystemUI::sdCopyResetResult_() {
  // Result is now managed by copyEngine_
  // Nothing to reset here
}

SystemUI::SdCopyDisplayStatus SystemUI::sdCopyStatus_() const {
  SdCopyDisplayStatus status;

  // Safety: only query engine if UI state indicates copy is active
  if (sdCopyUiState_ != SdCopyUiState::Running) {
    return status;  // Return default/empty status
  }

  status.running = (copyEngine_.getState() == SDCopyEngine::State::Running);
  status.fileCount = copyEngine_.getFileCount();
  status.filesProcessed = copyEngine_.getCurrentFileIndex();
  if (status.running && status.filesProcessed < status.fileCount) {
    status.filesProcessed += 1;  // Show current file as being processed
  }
  status.bytesDone = copyEngine_.getBytesDone();
  status.bytesTotal = copyEngine_.getTotalBytes();
  return status;
}

bool SystemUI::showTransferScreen_() {
  // Transfer screen works independently - no need to activate slideshow
  // Slideshow will be updated via focusTransferredFile() callback after transfer

  if (activeScreen_ != Screen::Transfer) {
    resetTransferUi_();
    enableTransfer_(true);
    transferFooter_ = String(i18n.t("buttons.transfer_actions"));

    // Ensure system font is loaded
    TextRenderer::ensureLoaded();

    // Clear screen immediately to hide any app content
    tft.fillScreen(TFT_BLACK);

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
  transferAutoExitAt_ = 0;
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
  if (index == 2) {
    if (e == BtnEvent::Single) {
      showSetup();
      return true;
    }
    if (e == BtnEvent::Long) {
      hide();
      return true;
    }
    return false;
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
  transferAutoExitAt_ = 0;  // Cancel any pending auto-exit
  transferNeedsClear_ = true;  // Clear screen for new transfer
}

void SystemUI::onTransferCompleted(TransferSource src, const char* filename, size_t size) {
  if (!isTransferActive()) {
    return;
  }

  // Don't call focusTransferredFile callback while Transfer screen is active
  // The slideshow will be updated when user exits to the app
  // Calling it here would cause the slideshow to draw immediately, creating a flash

  transferSource_ = src;
  transferState_ = TransferState::Completed;
  transferFilename_ = filename ? String(filename) : String();
  transferBytesExpected_ = size;
  transferBytesReceived_ = size;
  transferDirty_ = true;
  transferNeedsClear_ = true;  // Clear entire screen to remove any slideshow remnants
  // Don't show status text - "Abgeschlossen" is already in headerTertiary
}

void SystemUI::onTransferError(TransferSource src, const char* message) {
  if (!isTransferActive()) {
    return;
  }
  transferSource_ = src;
  transferState_ = TransferState::Error;
  transferMessage_ = message ? String(message) : String(i18n.t("transfer.failed"));
  transferDirty_ = true;
  transferNeedsClear_ = true;  // Clear entire screen to remove any slideshow remnants
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
  transferNeedsClear_ = true;  // Clear entire screen to remove any slideshow remnants
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
    headerSub = String(i18n.t("transfer.reception"));  // "Empfang"
    headerTertiary = String(i18n.t("transfer.completed"));  // "abgeschlossen"
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
  secondaryY = primaryY + line + 15;
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
      primary = String(transferSourceLabel_(transferSource_)) + String(": ") + String(i18n.t("transfer.receiving"));
      if (transferBytesExpected_ > 0) {
        uint32_t pct = static_cast<uint32_t>(
            (transferBytesReceived_ * 100UL) / transferBytesExpected_);
        String recv = formatKilobytes_(transferBytesReceived_);
        String total = formatKilobytes_(transferBytesExpected_);
        secondary = String(pct) + String("% (") + recv + String("/") + total + String(" KB)");
      } else {
        secondary = String(transferBytesReceived_) + String(" ") + String(i18n.t("transfer.bytes_received"));
      }
      break;
    }
    case TransferState::Completed:
      primary = transferFilename_.isEmpty() ? String() : transferFilename_;
      secondary = String(i18n.t("transfer.ready_to_receive"));
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
  }
  // Don't show "start_transfer" text in Idle state - header already says "Im Webtool starten"

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

  // Ensure system font is loaded
  TextRenderer::ensureLoaded();

  activeScreen_ = Screen::Language;
}

bool SystemUI::handleLanguageButtons_(uint8_t index, BtnEvent e) {
  Serial.printf("[SystemUI] handleLanguageButtons index=%d, event=%d\n", index, (int)e);

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
      // Cycle through languages + exit option
      uint8_t langCount = i18n.languageCount();
      languageSelection_ = (languageSelection_ + 1) % (langCount + 1);
      languageDirty_ = true;
      return true;
    }
    case BtnEvent::Long: {
      // Confirm selection
      uint8_t langCount = i18n.languageCount();
      Serial.printf("[SystemUI] Language Long press, selection: %d, langCount: %d\n",
                    languageSelection_, langCount);

      if (languageSelection_ == langCount) {
        // Exit option selected
        showSetup();
        setupMenu_.draw(true);
        return true;
      }

      if (languageSelection_ < langCount) {
        const char* selectedLang = i18n.availableLanguages(languageSelection_);
        Serial.printf("[SystemUI] Selected lang pointer: %p\n", selectedLang);
        if (selectedLang) {
          Serial.printf("[SystemUI] Calling setLanguage('%s')\n", selectedLang);
          i18n.setLanguage(selectedLang);
          // Force immediate redraw to show new language
          languageDirty_ = true;
          drawLanguageSelection_();
        } else {
          Serial.printf("[SystemUI] Language index %d returned NULL!\n", languageSelection_);
        }
      } else {
        Serial.printf("[SystemUI] Invalid: selection=%d, count=%d\n",
                      languageSelection_, langCount);
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
  uint8_t langCount = i18n.languageCount();

  const char* langNameKeys[] = {
    "language.german",
    "language.english",
    "language.french",
    "language.italian"
  };

  for (uint8_t i = 0; i < langCount && i < 4; ++i) {
    int16_t y = top + line + spacing + 15 + static_cast<int16_t>(i) * (line + spacing);
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

  // Exit option
  int16_t exitY = top + line + spacing + 15 + static_cast<int16_t>(langCount) * (line + spacing);
  String exitLabel = String(i18n.t("language.exit"));
  uint16_t exitColor = TFT_DARKGREY;

  if (languageSelection_ == langCount) {
    exitLabel = String("> ") + exitLabel;
    exitColor = TFT_WHITE;
  }

  TextRenderer::drawCentered(exitY, exitLabel, exitColor, TFT_BLACK);

  // Button hints at bottom
  const int16_t helperLine = TextRenderer::helperLineHeight();
  const int16_t hintY = TFT_H - (helperLine * 2) - 8 - 20;
  TextRenderer::drawHelperCentered(hintY, i18n.t("buttons.short_switch"), TFT_WHITE, TFT_BLACK);
  TextRenderer::drawHelperCentered(hintY + helperLine + 2,
                                  i18n.t("buttons.long_confirm"), TFT_WHITE, TFT_BLACK);
}
