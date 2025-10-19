#pragma once
#include "Core/App.h"
#include <Arduino.h>

class RandomPixelIntoneApp : public App {
public:
  const char* name() const override { return "Pixel Intone"; }
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

  void reseed_();
  void drift_();
  uint16_t colorForPixel_() const;
  void drawBurst_();
};
