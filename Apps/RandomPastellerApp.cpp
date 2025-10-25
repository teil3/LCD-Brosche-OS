#include "RandomPastellerApp.h"

#include <cmath>
#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Palette.h"
#include "Core/TextRenderer.h"

namespace {
constexpr float kTwoPiPasteller = 6.28318530718f;

inline uint8_t clamp8p(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}
}

float RandomPastellerApp::randUnit_() {
  return static_cast<float>(esp_random() & 0xFFFFFF) * (1.0f / 16777216.0f);
}

int32_t RandomPastellerApp::randInt_(int32_t max_exclusive) {
  if (max_exclusive <= 0) return 0;
  return static_cast<int32_t>(esp_random() % static_cast<uint32_t>(max_exclusive));
}

uint16_t RandomPastellerApp::pack565_(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void RandomPastellerApp::reseedAll_() {
  for (auto& blob : blobs_) {
    reseedBlob_(blob);
  }
}

void RandomPastellerApp::reseedBlob_(Blob& blob) {
  blob.x = randUnit_() * (TFT_W - 1);
  blob.y = randUnit_() * (TFT_H - 1);
  float angle = randUnit_() * kTwoPiPasteller;
  float speed = kSpeed * (0.25f + randUnit_());
  blob.vx = std::cos(angle) * speed;
  blob.vy = std::sin(angle) * speed;
  blob.radius = kMinRadius + (kMaxRadius - kMinRadius) * randUnit_();
  blob.r = static_cast<uint8_t>(randInt_(256));
  blob.g = static_cast<uint8_t>(randInt_(256));
  blob.b = static_cast<uint8_t>(randInt_(256));
}

void RandomPastellerApp::driftBlob_(Blob& blob) {
  // Mild velocity wander.
  blob.vx += (randUnit_() - 0.5f) * 6.0f;
  blob.vy += (randUnit_() - 0.5f) * 6.0f;

  // Clamp overall speed.
  float speed = std::sqrt(blob.vx * blob.vx + blob.vy * blob.vy);
  if (speed > kSpeed) {
    float scale = kSpeed / speed;
    blob.vx *= scale;
    blob.vy *= scale;
  }

  blob.x += blob.vx * (kBurstIntervalMs / 1000.0f);
  blob.y += blob.vy * (kBurstIntervalMs / 1000.0f);

  // Bounce off edges with a bit of damping.
  if (blob.x < 0.0f) {
    blob.x = 0.0f;
    blob.vx = std::abs(blob.vx) * 0.8f;
  } else if (blob.x > TFT_W - 1) {
    blob.x = TFT_W - 1;
    blob.vx = -std::abs(blob.vx) * 0.8f;
  }
  if (blob.y < 0.0f) {
    blob.y = 0.0f;
    blob.vy = std::abs(blob.vy) * 0.8f;
  } else if (blob.y > TFT_H - 1) {
    blob.y = TFT_H - 1;
    blob.vy = -std::abs(blob.vy) * 0.8f;
  }

  // Slowly drift radius within bounds.
  blob.radius += (randUnit_() - 0.5f) * 4.5f;
  if (blob.radius < kMinRadius) blob.radius = kMinRadius;
  if (blob.radius > kMaxRadius) blob.radius = kMaxRadius;

  auto randColorOffset = [&]() -> int {
    int span = 2 * kColorDrift + 1;
    return randInt_(span) - kColorDrift;
  };
  blob.r = clamp8p(blob.r + randColorOffset());
  blob.g = clamp8p(blob.g + randColorOffset());
  blob.b = clamp8p(blob.b + randColorOffset());
}

void RandomPastellerApp::drawBurst_(Blob& blob) {
  for (uint16_t i = 0; i < kPixelsPerBurst; ++i) {
    float angle = randUnit_() * kTwoPiPasteller;
    float falloffBias = std::pow(randUnit_(), 0.45f); // concentrate nearer the center
    float distance = blob.radius * falloffBias;
    float dx = std::cos(angle) * distance;
    float dy = std::sin(angle) * distance;

    // Add some jitter to break symmetry.
    dx += (randUnit_() - 0.5f) * 2.4f;
    dy += (randUnit_() - 0.5f) * 2.4f;

    int16_t px = static_cast<int16_t>(std::round(blob.x + dx));
    int16_t py = static_cast<int16_t>(std::round(blob.y + dy));
    if (px < 0 || px >= TFT_W || py < 0 || py >= TFT_H) {
      continue;
    }

    float normalized = 1.0f - (distance / (blob.radius + 0.01f));
    float highlight = normalized * normalized;
    float chromaBoost = 0.6f + 0.4f * normalized;

    auto addVariation = [&](uint8_t base) -> uint8_t {
      int variation = static_cast<int>((randUnit_() - 0.5f) * 140.0f * highlight);
      int value = static_cast<int>(base * chromaBoost) + variation;
      return clamp8p(value);
    };

    uint8_t r = addVariation(blob.r);
    uint8_t g = addVariation(blob.g);
    uint8_t b = addVariation(blob.b);

    // Occasional bright sparkle to introduce focal points.
    if (randUnit_() < 0.045f) {
      r = clamp8p(r + 50);
      g = clamp8p(g + 50);
      b = clamp8p(b + 50);
    }

    Palette::apply(palette_mode_, r, g, b, r, g, b);

    tft.drawPixel(px, py, pack565_(r, g, b));
  }
}

void RandomPastellerApp::clearCanvas_() {
  tft.fillScreen(TFT_BLACK);
}

void RandomPastellerApp::init() {
  time_accum_ = 0;
  pause_until_ = 0;
  palette_mode_ = 0;
  clearCanvas_();
  reseedAll_();

  // Prime a few bursts so the first frame is not empty.
  for (uint8_t i = 0; i < kBlobCount; ++i) {
    for (uint8_t j = 0; j < 4; ++j) {
      drawBurst_(blobs_[i]);
    }
  }
}

void RandomPastellerApp::tick(uint32_t delta_ms) {
  if (pause_until_) {
    uint32_t now = millis();
    if (now < pause_until_) return;
    pause_until_ = 0;
  }
  time_accum_ += delta_ms;
  while (time_accum_ >= kBurstIntervalMs) {
    time_accum_ -= kBurstIntervalMs;
    // Draw a few bursts each interval to emulate painterly strokes.
    for (uint8_t iter = 0; iter < 3; ++iter) {
      Blob& blob = blobs_[randInt_(kBlobCount)];
      drawBurst_(blob);
    }
    for (auto& blob : blobs_) {
      driftBlob_(blob);
    }
  }
}

void RandomPastellerApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      // Recolour existing blobs.
      for (auto& blob : blobs_) {
        blob.r = static_cast<uint8_t>(randInt_(256));
        blob.g = static_cast<uint8_t>(randInt_(256));
        blob.b = static_cast<uint8_t>(randInt_(256));
      }
      break;
    case BtnEvent::Double:
      clearCanvas_();
      break;
    case BtnEvent::Long:
      nextPalette_();
      clearCanvas_();
      reseedAll_();
      showStatus_(String("Palette ") + paletteName_());
      break;
    default:
      break;
  }
}

void RandomPastellerApp::shutdown() {
  // No persistent state.
}

void RandomPastellerApp::nextPalette_() {
  palette_mode_ = Palette::nextMode(palette_mode_);
}

void RandomPastellerApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pause_until_ = millis() + 1000;
  time_accum_ = 0;
}

const char* RandomPastellerApp::paletteName_() const {
  return Palette::modeName(palette_mode_);
}
