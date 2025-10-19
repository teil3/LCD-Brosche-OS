#include "RandomSmallPixelIntoneApp.h"

#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"

namespace {
constexpr uint8_t kVariation = 4;
constexpr uint16_t kBurstPixels = 50;
constexpr uint32_t kBurstIntervalMs = 10;
constexpr uint8_t kBaseDrift = 24; // how far the base can wander per burst

uint8_t rand8() { return static_cast<uint8_t>(esp_random() & 0xFF); }

uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
}

void RandomSmallPixelIntoneApp::reseedBaseColor_() {
  base_r_ = rand8();
  base_g_ = rand8();
  base_b_ = rand8();
}

void RandomSmallPixelIntoneApp::driftBaseColor_() {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kBaseDrift + 1;
    return static_cast<int>(esp_random() % span) - kBaseDrift;
  };
  base_r_ = static_cast<uint8_t>(clamp(base_r_ + randOffset()));
  base_g_ = static_cast<uint8_t>(clamp(base_g_ + randOffset()));
  base_b_ = static_cast<uint8_t>(clamp(base_b_ + randOffset()));
}

uint16_t RandomSmallPixelIntoneApp::randomColor_() const {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kVariation + 1;
    return static_cast<int>(esp_random() % span) - kVariation;
  };
  int r = clamp(base_r_ + randOffset());
  int g = clamp(base_g_ + randOffset());
  int b = clamp(base_b_ + randOffset());
  return pack565(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

void RandomSmallPixelIntoneApp::drawBurst_() {
  for (uint16_t i = 0; i < kBurstPixels; ++i) {
    uint16_t x = esp_random() % TFT_W;
    uint16_t y = esp_random() % TFT_H;
    uint16_t color = randomColor_();
    tft.drawPixel(x, y, color);
  }
  driftBaseColor_();
}

void RandomSmallPixelIntoneApp::init() {
  timeAccum_ = 0;
  tft.fillScreen(TFT_BLACK);
  reseedBaseColor_();
  drawBurst_();
}

void RandomSmallPixelIntoneApp::tick(uint32_t delta_ms) {
  timeAccum_ += delta_ms;
  while (timeAccum_ >= kBurstIntervalMs) {
    timeAccum_ -= kBurstIntervalMs;
    drawBurst_();
  }
}

void RandomSmallPixelIntoneApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      reseedBaseColor_();
      drawBurst_();
      break;
    case BtnEvent::Double:
      tft.fillScreen(TFT_BLACK);
      drawBurst_();
      break;
    case BtnEvent::Long:
      tft.fillScreen(TFT_BLACK);
      reseedBaseColor_();
      drawBurst_();
      break;
    default:
      break;
  }
}

void RandomSmallPixelIntoneApp::shutdown() {
  // Nothing persistent to clean up.
}
