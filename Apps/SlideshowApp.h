#pragma once

#include <Arduino.h>
#include <vector>

#include "Core/App.h"
#include "Core/Storage.h"
#include "Core/I18n.h"

class SlideshowApp : public App {
public:
  String dir = "/";
  uint32_t dwell_ms = 5000;
  bool show_filename = false;
  bool auto_mode = true;  // true = Auto-Slideshow, false = Manuell/Delete

  const char* name() const override { return i18n.t("apps.slideshow"); }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;
  void resume() override;

  bool setSlideSource(SlideSource src, bool showToast = true, bool renderNow = true);
  SlideSource slideSource() const { return source_; }
  String sourceLabel() const;
  void setUiLocked(bool locked);
  bool isUiLocked() const { return uiLocked_; }
  bool focusTransferredFile(const char* filename, size_t size);

private:
  enum class ControlMode : uint8_t { Auto = 0, Manual = 1, DeleteMenu = 2 };
  enum class MenuScreen : uint8_t { None = 0, Slideshow, Source, AutoSpeed };
  enum class DeleteState : uint8_t {
    Idle = 0,
    DeleteAllConfirm,
    DeleteSingle,
    DeleteSingleConfirm,
    Error,
    Done
  };

  std::vector<String> files_;
  size_t idx_ = 0;
  uint32_t timeSinceSwitch_ = 0;
  uint8_t dwellIdx_ = 1;  // 0=1s,1=5s,2=10s,3=30s,4=300s
  String toastText_;
  uint32_t toastUntil_ = 0;
  bool toastDirty_ = false;
  ControlMode controlMode_ = ControlMode::Auto;
  SlideSource source_ = SlideSource::SDCard;
  bool manualFilenameActive_ = false;
  uint32_t manualFilenameUntil_ = 0;
  String manualFilenameLabel_;
  bool manualFilenameDirty_ = false;
  DeleteState deleteState_ = DeleteState::Idle;
  uint8_t deleteMenuSelection_ = 0;     // 0=Alle löschen, 1=Einzeln, 2=Exit
  uint8_t deleteConfirmSelection_ = 0;  // 0=Nein, 1=Ja, 2=Exit
  bool deleteMenuDirty_ = true;
  bool deleteConfirmDirty_ = true;
  uint32_t deleteSingleTimer_ = 0;
  String deleteCurrentFile_;
  size_t deleteCount_ = 0;
  bool uiLocked_ = false;
  MenuScreen menuScreen_ = MenuScreen::None;
  uint8_t slideshowMenuSelection_ = 0;
  uint8_t sourceMenuSelection_ = 0;
  uint8_t autoSpeedSelection_ = 0;
  bool slideshowMenuDirty_ = false;
  bool sourceMenuDirty_ = false;
  bool autoSpeedMenuDirty_ = false;
  String helperLinePrimary_;
  String helperLineSecondary_;
  String helperLineTertiary_;
  uint32_t helperLinesUntil_ = 0;
  bool helperLinesDirty_ = false;

  void setControlMode_(ControlMode mode, bool showToast = true);
  void setSource_(SlideSource src, bool showToast = true);
  void showCurrent_(bool allowManualOverlay = true, bool clearScreen = true);
  bool isJpeg_(const String& n);
  void advance_(int step);
  void applyDwell_();
  String dwellLabel_() const;
  String modeLabel_() const;
  String sourceLabel_() const;
  String dwellToastLabel_() const;
  void showToast_(const String& txt, uint32_t duration_ms);
  void drawToastOverlay_();
  void drawManualFilenameOverlay_();
  void drawHelperOverlay_();
  void showHelperOverlay_(const String& primary,
                          const String& secondary,
                          const String& tertiary,
                          uint32_t duration_ms);
  void clearHelperOverlay_();
  bool rebuildFileList_();
  bool rebuildFileListFrom_(SlideSource src);
  bool readDirectoryEntries_(fs::FS* fs, const String& basePath, std::vector<String>& out);
  bool ensureFlashReady_();
  bool ensureSdReady_();
  void openSlideshowMenu_();
  void closeMenus_();
  void openSourceMenu_();
  void openAutoSpeedMenu_();
  void handleMenuButton_(BtnEvent e);
  void handleSourceMenuButton_(BtnEvent e);
  void handleAutoSpeedMenuButton_(BtnEvent e);
  void drawSlideshowMenu_();
  void drawSourceMenu_();
  void drawAutoSpeedMenu_();
  String dwellOptionLabel_(uint8_t idx) const;
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
  void returnToSlideshowMenu_();
};
