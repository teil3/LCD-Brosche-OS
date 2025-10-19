#include "RandomPixelIntoneApp.h"

#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"

namespace {
constexpr uint8_t kIntoneVariation = 4;
constexpr uint16_t kIntoneBurstPx = 50;
constexpr uint32_t kIntoneIntervalMs = 10;
constexpr uint8_t kIntoneStep = 5;
constexpr uint8_t kIntoneBaseDrift = 24;

inline uint8_t rand8Intone() { return static_cast<uint8_t>(esp_random() & 0xFF); }
inline uint16_t randCoordIntone(uint16_t max) { return static_cast<uint16_t>(esp_random() % max); }

uint16_t pack565Intone(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
}

void RandomPixelIntoneApp::reseed_() {
  base_r_ = rand8Intone();
  base_g_ = rand8Intone();
  base_b_ = rand8Intone();
}

void RandomPixelIntoneApp::drift_() {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kIntoneBaseDrift + 1;
    return static_cast<int>(esp_random() % span) - kIntoneBaseDrift;
  };
  base_r_ = static_cast<uint8_t>(clamp(base_r_ + randOffset()));
  base_g_ = static_cast<uint8_t>(clamp(base_g_ + randOffset()));
  base_b_ = static_cast<uint8_t>(clamp(base_b_ + randOffset()));
}

uint16_t RandomPixelIntoneApp::colorForPixel_() const {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kIntoneVariation + 1;
    return static_cast<int>(esp_random() % span) - kIntoneVariation;
  };
  int r = clamp(base_r_ + randOffset());
  int g = clamp(base_g_ + randOffset());
  int b = clamp(base_b_ + randOffset());
  return pack565Intone(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

void RandomPixelIntoneApp::drawBurst_() {
  for (uint16_t i = 0; i < kIntoneBurstPx; ++i) {
    uint16_t x = randCoordIntone(TFT_W);
    uint16_t y = randCoordIntone(TFT_H);
    x = (x / kIntoneStep) * kIntoneStep;
    y = (y / kIntoneStep) * kIntoneStep;
    tft.fillRect(x, y, kIntoneStep, kIntoneStep, colorForPixel_());
  }
  drift_();
}

void RandomPixelIntoneApp::init() {
  timeAccum_ = 0;
  tft.fillScreen(TFT_BLACK);
  reseed_();
  drawBurst_();
}

void RandomPixelIntoneApp::tick(uint32_t delta_ms) {
  timeAccum_ += delta_ms;
  while (timeAccum_ >= kIntoneIntervalMs) {
    timeAccum_ -= kIntoneIntervalMs;
    drawBurst_();
  }
}

void RandomPixelIntoneApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      reseed_();
      drawBurst_();
      break;
    case BtnEvent::Double:
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      break;
    case BtnEvent::Long:
      reseed_();
      drawBurst_();
      break;
    default:
      break;
  }
}

void RandomPixelIntoneApp::shutdown() {
  // Nothing persistent to clean.
}
