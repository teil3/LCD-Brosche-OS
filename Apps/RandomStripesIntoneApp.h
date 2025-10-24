#pragma once

#include "Core/App.h"
#include <Arduino.h>

class RandomStripesIntoneApp : public App {
public:
  const char* name() const override { return "Streifen-Töne"; }

  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {}
  void shutdown() override;

private:
  uint8_t base_r_ = 0;
  uint8_t base_g_ = 0;
  uint8_t base_b_ = 0;

  uint8_t stepIndex_ = 0;
  uint8_t intervalIndex_ = 0;
  uint32_t timeAccum_ = 0;
  uint32_t pauseUntil_ = 0;

  void reseed_();
  void drift_();
  uint16_t colorForStripe_() const;
  void drawBurst_();
  void nextStep_();
  void slower_();
  void showStatus_(const String& msg);

  uint16_t currentStripeHeight_() const;
  uint32_t currentInterval_() const;
};
