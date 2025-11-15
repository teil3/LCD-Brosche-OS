#pragma once

#include <functional>
#include <vector>
#include <array>

#include <Arduino.h>

#include "Core/SetupMenu.h"
#include "Core/Storage.h"
#include "Core/App.h"

class App;

class SystemUI {
 public:
  enum class TransferSource : uint8_t { None = 0, Usb, Ble };

  struct Callbacks {
    std::function<bool()> ensureSlideshowActive;
    std::function<bool(SlideSource)> setSource;
    std::function<bool(const char* filename, size_t size)> focusTransferredFile;
  };

  void begin(const Callbacks& cb);

  void showSetup(App* activeApp = nullptr);
  void hide();
  bool isActive() const { return activeScreen_ != Screen::None; }
  bool shouldPauseApps() const { return isActive(); }

  // Returns true if the event was consumed.
  bool handleButton(uint8_t index, BtnEvent e);

  void draw();
  void onTransferStarted(TransferSource src, const char* filename, size_t size);
  void onTransferCompleted(TransferSource src, const char* filename, size_t size);
  void onTransferError(TransferSource src, const char* message);
  void onTransferAborted(TransferSource src, const char* message);
  bool isTransferActive() const { return activeScreen_ == Screen::Transfer; }

 private:
  enum class Screen : uint8_t { None = 0, Setup, Language, SdCopyConfirm, SdCopyProgress, Transfer };
  enum class SdCopyState : uint8_t { Idle = 0, Confirm, Running };
  enum class SdCopyOutcome : uint8_t { None = 0, Success, Error, Aborted };
  enum class CopyFileType : uint8_t { Bootlogo = 0, Config, Font, Lua, Jpg };
  enum class TransferState : uint8_t { Idle = 0, Receiving, Completed, Error, Aborted };

  struct CopyItem {
    String path;
    String name;
    size_t size = 0;
    CopyFileType type = CopyFileType::Jpg;
    String destPath;
  };

  struct SdCopyDisplayStatus {
    bool running = false;
    size_t filesProcessed = 0;
    size_t fileCount = 0;
    size_t bytesDone = 0;
    size_t bytesTotal = 0;
  };

  bool handleSetupButtons_(uint8_t index, BtnEvent e);
  bool handleLanguageButtons_(uint8_t index, BtnEvent e);
  bool handleSdCopyConfirmButtons_(uint8_t index, BtnEvent e);
  bool handleSdCopyProgressButtons_(uint8_t index, BtnEvent e);
  bool handleTransferButtons_(uint8_t index, BtnEvent e);
  void handleSetupSelection_();
  void showLanguageSelection_();
  void drawLanguageSelection_();
  void showSdCopyConfirm_();
  void showSdCopyProgress_();
  void drawSdCopyConfirm_();
  void drawSdCopyProgress_();
  void showSdCopyStatus_(const String& text, uint32_t duration_ms = 1200, uint16_t color = TFT_WHITE);
  void resetSdCopyUi_();
  bool sdCopyStartConfirm_();
  bool sdCopyBegin_();
  void sdCopyCancel_();
  void sdCopyAbort_();
  void sdCopyTick_();
  void sdCopyResetResult_();
  void sdCopyFinalize_(SdCopyOutcome outcome, const String& message);
  bool sdCopyPrepareQueue_();
  bool sdCopyEnsureFlashReady_();
  bool sdCopyEnsureSdReady_();
  void sdCopyCloseFiles_();
  SdCopyDisplayStatus sdCopyStatus_() const;
  bool showTransferScreen_();
  void drawTransfer_();
  void resetTransferUi_();
  void enableTransfer_(bool enable);
  void updateTransferProgress_();
  void showTransferStatus_(const String& text, uint32_t duration_ms = 1500, uint16_t color = TFT_WHITE);
  const char* transferSourceLabel_(TransferSource src) const;
  String formatKilobytes_(size_t bytes) const;

  Screen activeScreen_ = Screen::None;
  Callbacks callbacks_;
  SetupMenu setupMenu_;
  uint8_t languageSelection_ = 0;
  bool languageDirty_ = true;
  uint8_t sdCopySelection_ = 1;
  bool sdCopyDirty_ = true;
  String sdCopyStatusText_;
  uint32_t sdCopyStatusUntil_ = 0;
  uint16_t sdCopyStatusColor_ = TFT_WHITE;
  bool sdCopyPendingExit_ = false;
  SdCopyState sdCopyState_ = SdCopyState::Idle;
  SdCopyOutcome sdCopyOutcome_ = SdCopyOutcome::None;
  String sdCopyOutcomeMessage_;
  bool sdCopyAbortRequest_ = false;
  std::vector<CopyItem> sdCopyQueue_;
  size_t sdCopyQueueIndex_ = 0;
  size_t sdCopyBytesTotal_ = 0;
  size_t sdCopyBytesDone_ = 0;
  File sdCopySrc_;
  File sdCopyDst_;
  size_t sdCopyFileBytesDone_ = 0;
  uint8_t* sdCopyBuffer_ = nullptr;  // Dynamic allocation to save 2KB when not copying
  SdCopyDisplayStatus sdCopyLastStatus_;
  bool sdCopyProgressNeedsClear_ = true;
  String sdCopyHeader_;
  uint16_t sdCopyBarFill_ = 0;
  String sdCopyHelperText_;
  uint16_t sdCopyHelperColor_ = TFT_WHITE;

  bool transferEnabled_ = false;
  bool transferDirty_ = true;
  bool transferNeedsClear_ = true;
  TransferSource transferSource_ = TransferSource::None;
  TransferState transferState_ = TransferState::Idle;
  String transferFilename_;
  String transferMessage_;
  size_t transferBytesExpected_ = 0;
  size_t transferBytesReceived_ = 0;
  uint16_t transferBarFill_ = 0;
  bool transferProgressFrameDrawn_ = false;
  String transferHeader_;
  String transferHeaderSub_;
  String transferHeaderTertiary_;
  String transferPrimary_;
  String transferSecondary_;
  String transferFooter_;
  String transferStatusText_;
  uint32_t transferStatusUntil_ = 0;
  uint16_t transferStatusColor_ = TFT_WHITE;
  uint32_t transferAutoExitAt_ = 0;

  App* activeApp_ = nullptr;  // Active app when overlay is shown
};
