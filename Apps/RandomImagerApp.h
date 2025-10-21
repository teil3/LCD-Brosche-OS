#pragma once

#include "Core/App.h"
#include <Arduino.h>

class RandomImagerApp : public App {
public:
  const char* name() const override { return "Random Imager"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override {}
  void shutdown() override;

private:
  struct Blob {
    float x;
    float y;
    float vx;
    float vy;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    float radius;
  };

  static constexpr uint8_t kBlobCount = 6;
  static constexpr uint16_t kPixelsPerBurst = 60;
  static constexpr uint32_t kBurstIntervalMs = 14;
  static constexpr uint8_t kColorDrift = 8;
  static constexpr float kMinRadius = 18.0f;
  static constexpr float kMaxRadius = 64.0f;
  static constexpr float kSpeed = 16.0f;

  Blob blobs_[kBlobCount];
  uint32_t time_accum_ = 0;

  static float randUnit_();          // [0,1)
  static int32_t randInt_(int32_t max_exclusive);
  static uint16_t pack565_(uint8_t r, uint8_t g, uint8_t b);

  void reseedAll_();
  void reseedBlob_(Blob& blob);
  void driftBlob_(Blob& blob);
  void drawBurst_(Blob& blob);
  void clearCanvas_();
};

