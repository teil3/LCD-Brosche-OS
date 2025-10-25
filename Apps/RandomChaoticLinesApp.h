#pragma once
#include "Core/App.h"
#include <Arduino.h>

class RandomChaoticLinesApp : public App {
public:
  const char* name() const override { return "Chaos-Linien"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {};
  void shutdown() override;

private:
  uint32_t timeAccum_ = 0;
  bool auto_mode_ = true;
  uint32_t pause_until_ = 0;
  uint8_t base_r_ = 0;
  uint8_t base_g_ = 0;
  uint8_t base_b_ = 0;
  uint8_t palette_mode_ = 0; // 0=Alle Farben, 1=Rot, 2=Grün, 3=Blau, 4=Graustufen, 5=Bunt

  void drawBurst_();
  uint16_t rndColor_() const;
  void reseedBase_();
  void driftBase_();
  void nextPalette_();
  void showStatus_(const String& msg);
  const char* paletteName_() const;
};
