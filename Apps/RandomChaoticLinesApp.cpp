#include "RandomChaoticLinesApp.h"

#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"

namespace {
constexpr uint16_t kLinesBurst = 50;
constexpr uint32_t kLinesIntervalMs = 100;

inline uint16_t rand16Lines() { return static_cast<uint16_t>(esp_random() & 0xFFFF); }

inline uint16_t randCoordLines(uint16_t max) {
  return static_cast<uint16_t>(esp_random() % max);
}
}

uint16_t RandomChaoticLinesApp::rndColor_() const { return rand16Lines(); }

void RandomChaoticLinesApp::drawBurst_() {
  for (uint16_t i = 0; i < kLinesBurst; ++i) {
    uint16_t x0 = randCoordLines(TFT_W);
    uint16_t y0 = randCoordLines(TFT_H);
    uint16_t x1 = randCoordLines(TFT_W);
    uint16_t y1 = randCoordLines(TFT_H);
    tft.drawLine(x0, y0, x1, y1, rndColor_());
  }
}

void RandomChaoticLinesApp::init() {
  timeAccum_ = 0;
  auto_mode_ = true;
  tft.fillScreen(TFT_BLACK);
  drawBurst_();
}

void RandomChaoticLinesApp::tick(uint32_t delta_ms) {
  if (!auto_mode_) return;

  timeAccum_ += delta_ms;
  while (timeAccum_ >= kLinesIntervalMs) {
    timeAccum_ -= kLinesIntervalMs;
    drawBurst_();
  }
}

void RandomChaoticLinesApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      drawBurst_();
      break;
    case BtnEvent::Double:
      tft.fillScreen(TFT_BLACK);
      break;
    case BtnEvent::Long:
      auto_mode_ = !auto_mode_;
      if (auto_mode_) timeAccum_ = 0;
      break;
    default:
      break;
  }
}

void RandomChaoticLinesApp::shutdown() {
  // nothing to clean up
}
