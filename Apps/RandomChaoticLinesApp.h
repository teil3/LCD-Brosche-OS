#pragma once
#include "Core/App.h"
#include "Core/I18n.h"
#include <Arduino.h>

class RandomChaoticLinesApp : public App {
public:
  const char* name() const override { return i18n.t("apps.chaos_lines"); }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {};
  void shutdown() override;

private:
  uint32_t timeAccum_ = 0;
  uint32_t pause_until_ = 0;
  uint8_t base_r_ = 0;
  uint8_t base_g_ = 0;
  uint8_t base_b_ = 0;
  uint8_t palette_mode_ = 0; // 0=Alle Farben, 1=Rot, 2=Grün, 3=Blau, 4=Graustufen, 5=Bunt
  uint16_t default_line_length_ = 0;
  uint16_t max_line_length_ = 0;
  uint8_t line_width_ = 1;

  static constexpr uint16_t kMinLineLength = 2;
  static constexpr uint8_t kMinLineWidth = 1;
  static constexpr uint8_t kMaxLineWidth = 4;

  void drawBurst_();
  void drawLineWithWidth_(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) const;
  uint16_t rndColor_() const;
  void reseedBase_();
  void driftBase_();
  void nextPalette_();
  void halveLineLength_();
  void widenLineWidth_();
  void showStatus_(const String& msg);
  const char* paletteName_() const;
};
