#pragma once
#include "Core/App.h"
#include "Core/Storage.h"
#include <vector>
#include <array>
#include <Arduino.h>
#include <LittleFS.h>

class SlideshowApp : public App {
public:
  String dir = "/";
  uint32_t dwell_ms = 5000;
  bool show_filename = false;

  // NEU:
  bool auto_mode = true;  // true=Auto-Slideshow, false=Manuell

  const char* name() const override { return "Diashow"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

  void onBleTransferStarted(const char* filename, size_t size);
  void onBleTransferCompleted(const char* filename, size_t size);
  void onBleTransferError(const char* message);
  void onBleTransferAborted(const char* message);
  void onUsbTransferStarted(const char* filename, size_t size);
  void onUsbTransferCompleted(const char* filename, size_t size);
  void onUsbTransferError(const char* message);
  void onUsbTransferAborted(const char* message);

private:
  enum class ControlMode : uint8_t { Auto = 0, Manual = 1, StorageMenu = 2, BleReceive = 3, DeleteMenu = 4 };
  enum class CopyState : uint8_t { Idle = 0, Confirm = 1, Running = 2, Done = 3, Error = 4, Aborted = 5 };
  enum class BleState : uint8_t { Idle = 0, Receiving = 1, Completed = 2, Error = 3, Aborted = 4 };
  enum class TransferSource : uint8_t { None = 0, Ble = 1, Usb = 2 };
  enum class DeleteState : uint8_t { Idle = 0, DeleteAllConfirm = 1, DeleteSingle = 2, DeleteSingleConfirm = 3, Deleting = 4, Done = 5, Error = 6 };

  enum class CopyFileType : uint8_t { Jpg = 0, Bootlogo = 1, Config = 2, Font = 3 };

  struct CopyItem {
    String path;
    String name;
    size_t size = 0;
    CopyFileType type = CopyFileType::Jpg;
    String destPath; // Ziel-Pfad im LittleFS
  };

  std::vector<String> files_;
  size_t idx_ = 0;
  uint32_t timeSinceSwitch_ = 0;
  uint8_t dwellIdx_ = 1; // 0=1s,1=5s,2=10s,3=30s,4=300s
  String toastText_;
  uint32_t toastUntil_ = 0;
  ControlMode controlMode_ = ControlMode::Auto;
  SlideSource source_ = SlideSource::SDCard;
  CopyState copyState_ = CopyState::Idle;
  bool copyAbortRequest_ = false;
  std::vector<CopyItem> copyQueue_;
  size_t copyQueueIndex_ = 0;
  size_t copyBytesTotal_ = 0;
  size_t copyBytesDone_ = 0;
  File copySrc_;
  File copyDst_;
  size_t copyFileBytesDone_ = 0;
  std::array<uint8_t, 2048> copyBuf_{};
  uint8_t copyConfirmSelection_ = 1; // 0=Nein, 1=Ja
  bool copyConfirmDirty_ = true;
  uint8_t copyConfirmLastSelection_ = 255;
  bool copyOverlayNeedsClear_ = true;
  uint32_t copyHeaderChecksum_ = 0;
  uint16_t copyBarFill_ = 0;
  bool copyHintDrawn_ = false;
  bool copyToastActive_ = false;
  String copyLastToast_;
  bool storageMenuDirty_ = true;
  String storageMenuLastSource_;
  String storageMenuLastFooter_;
  bool storageMenuLastToastActive_ = false;
  bool toastDirty_ = false;
  BleState bleState_ = BleState::Idle;
  TransferSource transferSource_ = TransferSource::None;
  bool bleOverlayDirty_ = true;
  String bleLastMessage_;
  String bleLastFilename_;
  size_t bleLastBytesReceived_ = 0;
  size_t bleLastBytesExpected_ = 0;
  bool bleOverlayNeedsClear_ = true;
  bool bleProgressFrameDrawn_ = false;
  uint16_t bleBarFill_ = 0;
  String bleLastHeader_;
  String bleLastPrimary_;
  String bleLastSecondary_;
  String bleLastFooter_;
  DeleteState deleteState_ = DeleteState::Idle;
  uint8_t deleteMenuSelection_ = 0; // 0=Alle löschen, 1=Einzeln
  uint8_t deleteConfirmSelection_ = 0; // 0=Nein, 1=Ja
  bool deleteMenuDirty_ = true;
  bool deleteConfirmDirty_ = true;
  uint32_t deleteSingleTimer_ = 0;
  String deleteCurrentFile_;
  size_t deleteCount_ = 0;

  void setControlMode_(ControlMode mode, bool showToast = true);
  void setSource_(SlideSource src, bool showToast = true);
  void toggleSource_();
  void enterStorageMenu_();
  void exitStorageMenu_();
  void requestCopy_();
  void cancelCopy_();
  void beginCopy_();
  void handleCopyTick_(uint32_t budget_ms);
  void closeCopyFiles_();
  void finalizeCopy_(CopyState state, const String& message);

  void showCurrent_();
  bool isJpeg_(const String& n);
  void advance_(int step);
  void applyDwell_();
  String dwellLabel_() const;
  String modeLabel_() const;
  String sourceLabel_() const;
  String dwellToastLabel_() const;
  void showToast_(const String& txt, uint32_t duration_ms);
  void drawToastOverlay_();
  void drawCopyOverlay_();
  void drawCopyConfirmOverlay_();
  void drawStorageMenuOverlay_();
  void drawBleReceiveOverlay_();
  bool rebuildFileList_();
  bool rebuildFileListFrom_(SlideSource src);
  bool readDirectoryEntries_(fs::FS* fs, const String& basePath, std::vector<String>& out);
  bool prepareCopyQueue_();
  bool ensureFlashReady_();
  bool ensureSdReady_();
  void markStorageMenuDirty_();
  void markCopyConfirmDirty_();
  const char* transferLabel_(TransferSource src) const;
  void handleTransferStarted_(TransferSource src, const char* filename, size_t size);
  void handleTransferCompleted_(TransferSource src, const char* filename, size_t size);
  void handleTransferError_(TransferSource src, const char* message);
  void handleTransferAborted_(TransferSource src, const char* message);
  void enterDeleteMenu_();
  void exitDeleteMenu_();
  void requestDeleteAll_();
  void confirmDeleteAll_();
  void cancelDeleteAll_();
  void startDeleteSingle_();
  void requestDeleteSingle_();
  void confirmDeleteSingle_();
  void cancelDeleteSingle_();
  void performDeleteAll_();
  void performDeleteSingle_(const String& path);
  void drawDeleteMenuOverlay_();
  void drawDeleteAllConfirmOverlay_();
  void drawDeleteSingleConfirmOverlay_();
  void markDeleteMenuDirty_();
  void markDeleteConfirmDirty_();
};
