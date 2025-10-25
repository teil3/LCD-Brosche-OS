#include "RandomSquareIntoneApp.h"

#include <algorithm>
#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Palette.h"
#include "Core/TextRenderer.h"

namespace {
constexpr uint8_t kSqColorVariation = 5;
constexpr uint8_t kSqBaseDrift = 20;
constexpr uint16_t kSqRectsPerBurst = 32;
constexpr uint8_t kSqMinSize = 4;
constexpr uint16_t kSqMaxSizeOptions[] = {14, 22, 32, 44, 64, 92};
constexpr uint8_t kSqMaxSizeCount = sizeof(kSqMaxSizeOptions) / sizeof(kSqMaxSizeOptions[0]);
constexpr uint32_t kSqIntervals[] = {14, 28, 56, 112, 180, 260, 380, 520, 760, 1120};
constexpr uint8_t kSqIntervalCount = sizeof(kSqIntervals) / sizeof(kSqIntervals[0]);

inline uint8_t clamp8sq(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}

inline uint8_t rand8sq() { return static_cast<uint8_t>(esp_random() & 0xFF); }
inline uint16_t randCoordSq(uint16_t max) { return static_cast<uint16_t>(esp_random() % max); }
inline int randOffsetSq(uint8_t max_delta) {
  int span = static_cast<int>(max_delta) * 2 + 1;
  return static_cast<int>(esp_random() % span) - static_cast<int>(max_delta);
}

uint16_t pack565sq(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
}  // namespace

void RandomSquareIntoneApp::reseed_() {
  base_r_ = rand8sq();
  base_g_ = rand8sq();
  base_b_ = rand8sq();
}

void RandomSquareIntoneApp::drift_() {
  base_r_ = clamp8sq(static_cast<int>(base_r_) + randOffsetSq(kSqBaseDrift));
  base_g_ = clamp8sq(static_cast<int>(base_g_) + randOffsetSq(kSqBaseDrift));
  base_b_ = clamp8sq(static_cast<int>(base_b_) + randOffsetSq(kSqBaseDrift));
}

uint16_t RandomSquareIntoneApp::randomColor_() const {
  uint8_t r = clamp8sq(static_cast<int>(base_r_) + randOffsetSq(kSqColorVariation));
  uint8_t g = clamp8sq(static_cast<int>(base_g_) + randOffsetSq(kSqColorVariation));
  uint8_t b = clamp8sq(static_cast<int>(base_b_) + randOffsetSq(kSqColorVariation));
  Palette::apply(palette_mode_, r, g, b, r, g, b);
  return pack565sq(r, g, b);
}

void RandomSquareIntoneApp::drawBurst_() {
  uint16_t maxSize = currentMaxSize_();
  if (maxSize < kSqMinSize) maxSize = kSqMinSize;

  uint16_t rects = kSqRectsPerBurst;
  if (maxSize > 64) rects = kSqRectsPerBurst / 2;
  if (currentInterval_() <= 40 && maxSize > 64) rects = std::max<uint16_t>(8, kSqRectsPerBurst / 3);

  for (uint16_t i = 0; i < rects; ++i) {
    uint16_t w = kSqMinSize + (esp_random() % (maxSize - kSqMinSize + 1));
    uint16_t h = kSqMinSize + (esp_random() % (maxSize - kSqMinSize + 1));

    // occasional streaks by elongating one axis.
    if (esp_random() & 0x1) {
      uint16_t extra = esp_random() % (maxSize + 1);
      if (esp_random() & 0x1) {
        w = std::min<uint16_t>(w + extra, maxSize);
      } else {
        h = std::min<uint16_t>(h + extra, maxSize);
      }
    }

    if (w >= TFT_W) w = TFT_W - 1;
    if (h >= TFT_H) h = TFT_H - 1;
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    uint16_t maxX = TFT_W > w ? static_cast<uint16_t>(TFT_W - w) : 0;
    uint16_t maxY = TFT_H > h ? static_cast<uint16_t>(TFT_H - h) : 0;
    uint16_t x = maxX ? randCoordSq(maxX + 1) : 0;
    uint16_t y = maxY ? randCoordSq(maxY + 1) : 0;

    tft.fillRect(x, y, w, h, randomColor_());

  }

  drift_();
}

void RandomSquareIntoneApp::init() {
  time_accum_ = 0;
  size_index_ = 0;
  interval_index_ = 0;
  pause_until_ = 0;
  palette_mode_ = 0;
  tft.fillScreen(TFT_BLACK);
  reseed_();
  drawBurst_();
}

void RandomSquareIntoneApp::tick(uint32_t delta_ms) {
  if (pause_until_) {
    uint32_t now = millis();
    if (now < pause_until_) {
      return;
    }
    pause_until_ = 0;
    time_accum_ = 0;
  }

  time_accum_ += delta_ms;
  uint32_t interval = currentInterval_();
  while (time_accum_ >= interval) {
    time_accum_ -= interval;
    drawBurst_();
  }
}

void RandomSquareIntoneApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      nextSize_();
      if (currentMaxSize_() > 64 && currentInterval_() < 112) {
        interval_index_ = 3; // ensure at least ~112ms when very large rectangles
      }
      tft.fillScreen(TFT_BLACK);
      reseed_();
      drawBurst_();
      showStatus_(String("Grösse <= ") + String(currentMaxSize_()));
      break;
    case BtnEvent::Double:
      nextInterval_();
      time_accum_ = 0;
      showStatus_(String("Bremse ") + String(currentInterval_()) + "ms");
      break;
    case BtnEvent::Long:
      nextPalette_();
      time_accum_ = 0;
      showStatus_(String("Palette ") + paletteName_());
      break;
    default:
      break;
  }
}

void RandomSquareIntoneApp::shutdown() {
  // No persistent state to clear.
}

void RandomSquareIntoneApp::nextSize_() {
  ++size_index_;
  if (size_index_ >= kSqMaxSizeCount) {
    size_index_ = 0;
  }
}

void RandomSquareIntoneApp::nextInterval_() {
  ++interval_index_;
  if (interval_index_ >= kSqIntervalCount) {
    interval_index_ = 0;
  }

  // if interval too fast for large sizes, reduce size automatically
  if (currentInterval_() < 112 && currentMaxSize_() > 64) {
    if (size_index_ > 3) size_index_ = 3; // clamp to size ≤ 44
  }
}

void RandomSquareIntoneApp::nextPalette_() {
  palette_mode_ = Palette::nextMode(palette_mode_);
}

void RandomSquareIntoneApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pause_until_ = millis() + 1000;
}

const char* RandomSquareIntoneApp::paletteName_() const {
  return Palette::modeName(palette_mode_);
}

uint16_t RandomSquareIntoneApp::currentMaxSize_() const {
  return kSqMaxSizeOptions[size_index_];
}

uint32_t RandomSquareIntoneApp::currentInterval_() const {
  return kSqIntervals[interval_index_];
}
