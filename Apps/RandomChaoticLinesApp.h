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

  void drawBurst_();
  uint16_t rndColor_() const;
};
