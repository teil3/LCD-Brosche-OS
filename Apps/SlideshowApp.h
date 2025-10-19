#pragma once
#include "Core/App.h"
#include <vector>
#include <Arduino.h>

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
  std::vector<String> files_;
  size_t idx_ = 0;
  uint32_t timeSinceSwitch_ = 0;
  uint8_t dwellIdx_ = 1; // 0=1s,1=5s,2=10s,3=30s,4=300s
  String toastText_;
  uint32_t toastUntil_ = 0;

  void showCurrent_();
  bool isJpeg_(const String& n);
  void advance_(int step);
  void applyDwell_();
  String dwellLabel_() const;
  String modeLabel_() const;
  String dwellToastLabel_() const;
  void showToast_(const String& txt, uint32_t duration_ms);
  void drawToastOverlay_();
};
