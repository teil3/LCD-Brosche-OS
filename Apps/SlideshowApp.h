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
  bool show_filename = true;

  // NEU:
  bool auto_mode = true;  // true=Auto-Slideshow, false=Manuell

  const char* name() const override { return "Slideshow"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

private:
  enum class ControlMode : uint8_t { Auto = 0, Manual = 1, StorageMenu = 2 };
  enum class CopyState : uint8_t { Idle = 0, Confirm = 1, Running = 2, Done = 3, Error = 4, Aborted = 5 };

  struct CopyItem {
    String name;
    size_t size = 0;
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
  bool storageMenuDirty_ = true;
  String storageMenuLastSource_;
  String storageMenuLastFooter_;
  bool storageMenuLastToastActive_ = false;

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
  bool rebuildFileList_();
  bool rebuildFileListFrom_(SlideSource src);
  bool readDirectoryEntries_(fs::FS* fs, const String& basePath, std::vector<String>& out);
  bool prepareCopyQueue_();
  bool ensureFlashReady_();
  void markStorageMenuDirty_();
  void markCopyConfirmDirty_();
};
