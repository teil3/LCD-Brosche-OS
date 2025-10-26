#include "RandomChaoticLinesApp.h"

#include <esp_random.h>
#include <math.h>

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

inline int16_t clampCoord(int32_t value, int32_t bound) {
  if (value < 0) {
    return 0;
  }
  if (value >= bound) {
    return static_cast<int16_t>(bound - 1);
  }
  return static_cast<int16_t>(value);
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
  int16_t maxDelta = static_cast<int16_t>(max_line_length_);
  if (maxDelta < 1) {
    maxDelta = 1;
  }
  int32_t limitSq = static_cast<int32_t>(max_line_length_) * static_cast<int32_t>(max_line_length_);
  if (limitSq <= 0) {
    limitSq = 1;
  }

  for (uint16_t i = 0; i < kLinesBurst; ++i) {
    uint16_t x0 = randCoordLines(TFT_W);
    uint16_t y0 = randCoordLines(TFT_H);

    int16_t dx = 0;
    int16_t dy = 0;
    uint8_t attempts = 0;
    do {
      dx = static_cast<int16_t>((esp_random() % (static_cast<uint32_t>(maxDelta) * 2 + 1)) -
                                maxDelta);
      dy = static_cast<int16_t>((esp_random() % (static_cast<uint32_t>(maxDelta) * 2 + 1)) -
                                maxDelta);
      ++attempts;
      if (attempts >= 12) {
        break;
      }
    } while ((dx == 0 && dy == 0) ||
             (static_cast<int32_t>(dx) * dx + static_cast<int32_t>(dy) * dy) > limitSq);

    if (dx == 0 && dy == 0) {
      dx = 1;
    }

    int32_t x1 = static_cast<int32_t>(x0) + dx;
    int32_t y1 = static_cast<int32_t>(y0) + dy;
    x1 = clampCoord(x1, TFT_W);
    y1 = clampCoord(y1, TFT_H);

    uint16_t color = rndColor_();
    if (x1 == x0 && y1 == y0) {
      tft.drawPixel(x0, y0, color);
      continue;
    }
    drawLineWithWidth_(static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                       static_cast<int16_t>(x1), static_cast<int16_t>(y1), color);
  }
  driftBase_();
}

void RandomChaoticLinesApp::drawLineWithWidth_(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                               uint16_t color) const {
  if (line_width_ <= 1) {
    tft.drawLine(x0, y0, x1, y1, color);
    return;
  }

  float dx = static_cast<float>(x1 - x0);
  float dy = static_cast<float>(y1 - y0);
  float length = sqrtf(dx * dx + dy * dy);
  if (length < 0.5f) {
    tft.drawPixel(x0, y0, color);
    return;
  }

  float nx = -dy / length;
  float ny = dx / length;
  for (uint8_t i = 0; i < line_width_; ++i) {
    float offset = static_cast<float>(2 * i - line_width_ + 1) / 2.0f;
    float fx = nx * offset;
    float fy = ny * offset;
    int16_t ox = static_cast<int16_t>(roundf(fx));
    int16_t oy = static_cast<int16_t>(roundf(fy));
    if (ox == 0 && oy == 0 && line_width_ > 1 && offset != 0.0f) {
      if (fabsf(nx) > fabsf(ny)) {
        oy = (offset > 0.0f) ? 1 : -1;
      } else {
        ox = (offset > 0.0f) ? 1 : -1;
      }
    }
    tft.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
  }
}

void RandomChaoticLinesApp::init() {
  timeAccum_ = 0;
  pause_until_ = 0;
  palette_mode_ = 0;
  float diag = sqrtf(static_cast<float>(TFT_W) * static_cast<float>(TFT_W) +
                     static_cast<float>(TFT_H) * static_cast<float>(TFT_H));
  default_line_length_ = static_cast<uint16_t>(diag);
  if (default_line_length_ < kMinLineLength) {
    default_line_length_ = kMinLineLength;
  }
  max_line_length_ = default_line_length_;
  line_width_ = kMinLineWidth;
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
      halveLineLength_();
      tft.fillScreen(TFT_BLACK);
      reseedBase_();
      drawBurst_();
      showStatus_(String("Länge ") + String(max_line_length_) + "px");
      break;
    case BtnEvent::Double:
      widenLineWidth_();
      tft.fillScreen(TFT_BLACK);
      reseedBase_();
      drawBurst_();
      showStatus_(String("Breite ") + String(line_width_) + "px");
      break;
    case BtnEvent::Long:
      nextPalette_();
      tft.fillScreen(TFT_BLACK);
      reseedBase_();
      drawBurst_();
      showStatus_(String("Palette ") + paletteName_());
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

void RandomChaoticLinesApp::halveLineLength_() {
  if (max_line_length_ <= kMinLineLength) {
    max_line_length_ = default_line_length_;
    return;
  }
  uint16_t next = static_cast<uint16_t>((max_line_length_ + 1) / 2);
  if (next < kMinLineLength) {
    next = kMinLineLength;
  }
  max_line_length_ = next;
}

void RandomChaoticLinesApp::widenLineWidth_() {
  if (line_width_ >= kMaxLineWidth) {
    line_width_ = kMinLineWidth;
  } else {
    ++line_width_;
  }
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
