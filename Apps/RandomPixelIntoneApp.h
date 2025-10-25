#pragma once
#include "Core/App.h"
#include <Arduino.h>

class RandomPixelIntoneApp : public App {
public:
  const char* name() const override { return "Pixel-Töne"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {};
  void shutdown() override;

private:
  uint8_t base_r_ = 0;
  uint8_t base_g_ = 0;
  uint8_t base_b_ = 0;
  uint32_t timeAccum_ = 0;
  uint8_t stepIndex_ = 0;
  uint8_t intervalIndex_ = 0;
  uint32_t pauseUntil_ = 0;
  uint8_t palette_mode_ = 0; // 0=Alle Farben, 1=Rot, 2=Grün, 3=Blau, 4=Graustufen, 5=Bunt

  void reseed_();
  void drift_();
  uint16_t colorForPixel_() const;
  void drawBurst_();
  void nextStep_();
  void slower_();
  void nextPalette_();
  void showStatus_(const String& msg);
  uint16_t currentStep_() const;
  uint32_t currentInterval_() const;
  const char* paletteName_() const;
};
