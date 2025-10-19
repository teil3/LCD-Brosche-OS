#pragma once
#include "Core/App.h"
#include <vector>
#include <Arduino.h>

class SlideshowApp : public App {
public:
  String dir = "/";
  uint32_t dwell_ms = 2000;
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
  uint32_t lastSwitch_ = 0;

  void showCurrent_();
  bool isJpeg_(const String& n);

  // NEU: kleines OSD für Modus-Anzeige
  void showModeOsd_();
};

