#include "RandomStripesIntoneApp.h"

#include <Arduino.h>
#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/TextRenderer.h"

namespace {
constexpr uint8_t kStripesVariation = 6;
constexpr uint16_t kStripesBurstLines = 60;
constexpr uint16_t kStripeHeights[] = {1, 2, 4, 8, 12, 16, 24};
constexpr uint8_t kStripeHeightCount = sizeof(kStripeHeights) / sizeof(kStripeHeights[0]);
constexpr uint32_t kStripeIntervals[] = {10, 20, 40, 80, 160, 240, 400, 600};
constexpr uint8_t kStripeIntervalCount = sizeof(kStripeIntervals) / sizeof(kStripeIntervals[0]);
constexpr uint8_t kStripesBaseDrift = 20;
constexpr uint16_t kStripeWidth = TFT_W;

inline uint8_t rand8Stripes() { return static_cast<uint8_t>(esp_random() & 0xFF); }
inline uint16_t randCoordStripes(uint16_t max) { return static_cast<uint16_t>(esp_random() % max); }

uint16_t pack565Stripes(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
}

void RandomStripesIntoneApp::reseed_() {
  base_r_ = rand8Stripes();
  base_g_ = rand8Stripes();
  base_b_ = rand8Stripes();
}

void RandomStripesIntoneApp::drift_() {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kStripesBaseDrift + 1;
    return static_cast<int>(esp_random() % span) - kStripesBaseDrift;
  };
  base_r_ = static_cast<uint8_t>(clamp(base_r_ + randOffset()));
  base_g_ = static_cast<uint8_t>(clamp(base_g_ + randOffset()));
  base_b_ = static_cast<uint8_t>(clamp(base_b_ + randOffset()));
}

uint16_t RandomStripesIntoneApp::colorForStripe_() const {
  auto clamp = [](int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
  };
  auto randOffset = []() {
    int span = 2 * kStripesVariation + 1;
    return static_cast<int>(esp_random() % span) - kStripesVariation;
  };
  int r = clamp(base_r_ + randOffset());
  int g = clamp(base_g_ + randOffset());
  int b = clamp(base_b_ + randOffset());
  return pack565Stripes(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

void RandomStripesIntoneApp::drawBurst_() {
  uint16_t height = currentStripeHeight_();
  if (height == 0) height = 1;
  if (height > TFT_H) height = TFT_H;
  uint16_t burstCount = kStripesBurstLines;
  if (height > kStripeHeights[0]) {
    burstCount = static_cast<uint16_t>((kStripesBurstLines * kStripeHeights[0]) / height);
    if (burstCount == 0) burstCount = 1;
  }

  for (uint16_t i = 0; i < burstCount; ++i) {
    uint16_t y = randCoordStripes(TFT_H);
    y = (y / height) * height;
    uint16_t maxY = (TFT_H > height) ? static_cast<uint16_t>(TFT_H - height) : 0;
    if (y > maxY) y = maxY;
    tft.fillRect(0, y, kStripeWidth, height, colorForStripe_());
  }
  drift_();
}

void RandomStripesIntoneApp::init() {
  timeAccum_ = 0;
  pauseUntil_ = 0;
  stepIndex_ = 0;
  intervalIndex_ = 0;
  tft.fillScreen(TFT_BLACK);
  reseed_();
  drawBurst_();
}

void RandomStripesIntoneApp::tick(uint32_t delta_ms) {
  if (pauseUntil_) {
    uint32_t now = millis();
    if (now < pauseUntil_) {
      return;
    }
    pauseUntil_ = 0;
    timeAccum_ = 0;
  }

  timeAccum_ += delta_ms;
  uint32_t interval = currentInterval_();
  while (timeAccum_ >= interval) {
    timeAccum_ -= interval;
    drawBurst_();
  }
}

void RandomStripesIntoneApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      nextStep_();
      Serial.printf("[StripesIntone] height=%u\n", currentStripeHeight_());
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      showStatus_(String("Hoehe ") + String(currentStripeHeight_()));
      break;
    case BtnEvent::Double:
      slower_();
      Serial.printf("[StripesIntone] interval=%lums\n", static_cast<unsigned long>(currentInterval_()));
      reseed_();
      drawBurst_();
      showStatus_(String("Bremse ") + String(currentInterval_()) + "ms");
      break;
    case BtnEvent::Long:
      intervalIndex_ = 0;
      Serial.printf("[StripesIntone] interval reset to %lums\n", static_cast<unsigned long>(currentInterval_()));
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      showStatus_(String("Bremse ") + String(currentInterval_()) + "ms");
      break;
    default:
      break;
  }
}

void RandomStripesIntoneApp::shutdown() {}

void RandomStripesIntoneApp::nextStep_() {
  ++stepIndex_;
  if (stepIndex_ >= kStripeHeightCount) {
    stepIndex_ = 0;
  }
}

void RandomStripesIntoneApp::slower_() {
  ++intervalIndex_;
  if (intervalIndex_ >= kStripeIntervalCount) {
    intervalIndex_ = 0;
  }
}

void RandomStripesIntoneApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pauseUntil_ = millis() + 1000;
}

uint16_t RandomStripesIntoneApp::currentStripeHeight_() const {
  return kStripeHeights[stepIndex_];
}

uint32_t RandomStripesIntoneApp::currentInterval_() const {
  return kStripeIntervals[intervalIndex_];
}

uint16_t RandomStripesIntoneApp::colorForStripe_() const;
