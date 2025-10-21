#pragma once

#include "Core/App.h"
#include <Arduino.h>

class RandomSquareIntoneApp : public App {
public:
  const char* name() const override { return "Rechteck-Toene"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {}
  void shutdown() override;

private:
  uint8_t base_r_ = 0;
  uint8_t base_g_ = 0;
  uint8_t base_b_ = 0;
  uint32_t time_accum_ = 0;
  uint8_t size_index_ = 0;
  uint8_t interval_index_ = 0;
  uint32_t pause_until_ = 0;
  uint8_t palette_mode_ = 0; // 0=full, 1=reds, 2=greens, 3=blues, 4=grayscale

  void reseed_();
  void drift_();
  uint16_t randomColor_() const;
  void drawBurst_();
  void nextSize_();
  void nextInterval_();
  void nextPalette_();
  void showStatus_(const String& msg);
  const char* paletteName_() const;
  uint16_t currentMaxSize_() const;
  uint32_t currentInterval_() const;
};
