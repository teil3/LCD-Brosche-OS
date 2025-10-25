#pragma once
#include "Core/App.h"
#include <Arduino.h>

class PixelFieldApp : public App {
public:
  const char* name() const override { return "Pixel-Feld"; }
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
  uint32_t pause_until_ = 0;
  uint8_t palette_mode_ = 0; // 0=Alle Farben, 1=Rot, 2=Grün, 3=Blau, 4=Graustufen, 5=Bunt

  void reseedBaseColor_();
  void driftBaseColor_();
  uint16_t randomColor_() const;
  void drawBurst_();
  void nextPalette_();
  void showStatus_(const String& msg);
  const char* paletteName_() const;
};
