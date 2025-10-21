#include "RandomPixelIntoneApp.h"

#include <Arduino.h>
#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/TextRenderer.h"

namespace {
constexpr uint8_t kIntoneVariation = 4;
constexpr uint16_t kIntoneBurstPx = 50;
constexpr uint16_t kIntoneSteps[] = {5, 10, 20, 40, 80, 160};
constexpr uint8_t kIntoneStepCount = sizeof(kIntoneSteps) / sizeof(kIntoneSteps[0]);
constexpr uint32_t kIntoneIntervals[] = {10, 20, 40, 80, 160, 240, 480, 960, 1920, 3840};
constexpr uint8_t kIntoneIntervalCount = sizeof(kIntoneIntervals) / sizeof(kIntoneIntervals[0]);
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
  uint16_t step = currentStep_();
  if (step > TFT_W) step = TFT_W;
  if (step > TFT_H) step = TFT_H;
  uint16_t burstCount = kIntoneBurstPx;
  if (step > kIntoneSteps[0]) {
    burstCount = static_cast<uint16_t>((kIntoneBurstPx * kIntoneSteps[0]) / step);
    if (burstCount == 0) burstCount = 1;
  }

  for (uint16_t i = 0; i < burstCount; ++i) {
    uint16_t x = randCoordIntone(TFT_W);
    uint16_t y = randCoordIntone(TFT_H);
    x = (x / step) * step;
    y = (y / step) * step;
    uint16_t maxX = (TFT_W > step) ? static_cast<uint16_t>(TFT_W - step) : 0;
    uint16_t maxY = (TFT_H > step) ? static_cast<uint16_t>(TFT_H - step) : 0;
    if (x > maxX) x = maxX;
    if (y > maxY) y = maxY;
    tft.fillRect(x, y, step, step, colorForPixel_());
  }
  drift_();
}

void RandomPixelIntoneApp::init() {
  timeAccum_ = 0;
  stepIndex_ = 0;
  intervalIndex_ = 0;
  pauseUntil_ = 0;
  tft.fillScreen(TFT_BLACK);
  reseed_();
  drawBurst_();
}

void RandomPixelIntoneApp::tick(uint32_t delta_ms) {
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

void RandomPixelIntoneApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      nextStep_();
      Serial.printf("[PixelIntone] step=%u\n", currentStep_());
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      showStatus_(String("Groesse ") + String(currentStep_()));
      break;
    case BtnEvent::Double:
      slower_();
      Serial.printf("[PixelIntone] interval=%lums\n", static_cast<unsigned long>(currentInterval_()));
      reseed_();
      drawBurst_();
      showStatus_(String("Bremse ") + String(currentInterval_()) + "ms");
      break;
    case BtnEvent::Long:
      intervalIndex_ = 0;
      Serial.printf("[PixelIntone] interval reset to %lums\n", static_cast<unsigned long>(currentInterval_()));
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      showStatus_(String("Bremse ") + String(currentInterval_()) + "ms");
      break;
    default:
      break;
  }
}

void RandomPixelIntoneApp::shutdown() {
  // Nothing persistent to clean.
}

void RandomPixelIntoneApp::nextStep_() {
  ++stepIndex_;
  if (stepIndex_ >= kIntoneStepCount) {
    stepIndex_ = 0;
  }
}

void RandomPixelIntoneApp::slower_() {
  ++intervalIndex_;
  if (intervalIndex_ >= kIntoneIntervalCount) {
    intervalIndex_ = 0;
  }
}

void RandomPixelIntoneApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pauseUntil_ = millis() + 1000;
}

uint16_t RandomPixelIntoneApp::currentStep_() const {
  return kIntoneSteps[stepIndex_];
}

uint32_t RandomPixelIntoneApp::currentInterval_() const {
  return kIntoneIntervals[intervalIndex_];
}
