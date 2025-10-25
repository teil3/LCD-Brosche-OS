#include "RandomChaoticLinesApp.h"

#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Palette.h"
#include "Core/TextRenderer.h"

namespace {
constexpr uint16_t kLinesBurst = 50;
constexpr uint32_t kLinesIntervalMs = 100;
constexpr uint8_t kLinesVariation = 6;
constexpr uint8_t kLinesBaseDrift = 24;

inline uint16_t randCoordLines(uint16_t max) {
  return static_cast<uint16_t>(esp_random() % max);
}
}

void RandomChaoticLinesApp::reseedBase_() {
  base_r_ = Palette::rand8();
  base_g_ = Palette::rand8();
  base_b_ = Palette::rand8();
}

void RandomChaoticLinesApp::driftBase_() {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kLinesBaseDrift + 1;
    return static_cast<int>(esp_random() % span) - kLinesBaseDrift;
  };
  base_r_ = static_cast<uint8_t>(clamp(base_r_ + randOffset()));
  base_g_ = static_cast<uint8_t>(clamp(base_g_ + randOffset()));
  base_b_ = static_cast<uint8_t>(clamp(base_b_ + randOffset()));
}

uint16_t RandomChaoticLinesApp::rndColor_() const {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kLinesVariation + 1;
    return static_cast<int>(esp_random() % span) - kLinesVariation;
  };
  uint8_t r = static_cast<uint8_t>(clamp(base_r_ + randOffset()));
  uint8_t g = static_cast<uint8_t>(clamp(base_g_ + randOffset()));
  uint8_t b = static_cast<uint8_t>(clamp(base_b_ + randOffset()));
  Palette::apply(palette_mode_, r, g, b, r, g, b);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void RandomChaoticLinesApp::drawBurst_() {
  for (uint16_t i = 0; i < kLinesBurst; ++i) {
    uint16_t x0 = randCoordLines(TFT_W);
    uint16_t y0 = randCoordLines(TFT_H);
    uint16_t x1 = randCoordLines(TFT_W);
    uint16_t y1 = randCoordLines(TFT_H);
    tft.drawLine(x0, y0, x1, y1, rndColor_());
  }
  driftBase_();
}

void RandomChaoticLinesApp::init() {
  timeAccum_ = 0;
  auto_mode_ = true;
  pause_until_ = 0;
  palette_mode_ = 0;
  reseedBase_();
  tft.fillScreen(TFT_BLACK);
  drawBurst_();
}

void RandomChaoticLinesApp::tick(uint32_t delta_ms) {
  if (pause_until_) {
    uint32_t now = millis();
    if (now < pause_until_) return;
    pause_until_ = 0;
  }
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
      nextPalette_();
      tft.fillScreen(TFT_BLACK);
      reseedBase_();
      drawBurst_();
      showStatus_(String("Palette ") + paletteName_());
      break;
    case BtnEvent::Long:
      auto_mode_ = !auto_mode_;
      if (auto_mode_) {
        timeAccum_ = 0;
      }
      showStatus_(auto_mode_ ? String("Auto an") : String("Auto aus"));
      break;
    default:
      break;
  }
}

void RandomChaoticLinesApp::shutdown() {
  // nothing to clean up
}

void RandomChaoticLinesApp::nextPalette_() {
  palette_mode_ = Palette::nextMode(palette_mode_);
}

void RandomChaoticLinesApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pause_until_ = millis() + 1000;
  timeAccum_ = 0;
}

const char* RandomChaoticLinesApp::paletteName_() const {
  return Palette::modeName(palette_mode_);
}
