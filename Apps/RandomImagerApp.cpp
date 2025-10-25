#include "RandomImagerApp.h"

#include <algorithm>
#include <cmath>
#include <esp_random.h>

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Palette.h"
#include "Core/TextRenderer.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;

inline uint8_t clamp8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}
}

float RandomImagerApp::randUnit_() {
  return static_cast<float>(esp_random() & 0xFFFFFF) * (1.0f / 16777216.0f);
}

int32_t RandomImagerApp::randInt_(int32_t max_exclusive) {
  if (max_exclusive <= 0) return 0;
  return static_cast<int32_t>(esp_random() % static_cast<uint32_t>(max_exclusive));
}

uint16_t RandomImagerApp::pack565_(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void RandomImagerApp::reseedAll_() {
  for (auto& blob : blobs_) {
    reseedBlob_(blob);
  }
}

void RandomImagerApp::reseedBlob_(Blob& blob) {
  blob.x = randUnit_() * (TFT_W - 1);
  blob.y = randUnit_() * (TFT_H - 1);
  float angle = randUnit_() * kTwoPi;
  float speed = kSpeed * (0.25f + randUnit_());
  blob.vx = std::cos(angle) * speed;
  blob.vy = std::sin(angle) * speed;
  blob.major = kMinAxis + (kMaxAxis - kMinAxis) * randUnit_();
  float aspect = 0.22f + randUnit_() * 0.65f;
  blob.minor = std::max(kMinAxis * 0.35f, blob.major * aspect);
  blob.angle = randUnit_() * kTwoPi;
  blob.wobble = randUnit_() * kTwoPi;
  blob.r = static_cast<uint8_t>(randInt_(256));
  blob.g = static_cast<uint8_t>(randInt_(256));
  blob.b = static_cast<uint8_t>(randInt_(256));
}

void RandomImagerApp::driftBlob_(Blob& blob) {
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

  // Slowly drift axes within bounds.
  blob.major += (randUnit_() - 0.5f) * 5.0f;
  blob.minor += (randUnit_() - 0.5f) * 4.0f;
  if (blob.major < kMinAxis) blob.major = kMinAxis;
  if (blob.major > kMaxAxis) blob.major = kMaxAxis;
  float minMinor = std::max(kMinAxis * 0.25f, blob.major * 0.18f);
  float maxMinor = blob.major * 0.9f;
  if (blob.minor < minMinor) blob.minor = minMinor;
  if (blob.minor > maxMinor) blob.minor = maxMinor;

  blob.angle += (randUnit_() - 0.5f) * kAngleDrift;
  if (blob.angle < 0.0f) blob.angle += kTwoPi;
  if (blob.angle >= kTwoPi) blob.angle -= kTwoPi;

  blob.wobble += 0.04f + randUnit_() * 0.08f;
  if (blob.wobble >= kTwoPi) blob.wobble -= kTwoPi;

  auto randColorOffset = [&]() -> int {
    int span = 2 * kColorDrift + 1;
    return randInt_(span) - kColorDrift;
  };
  blob.r = clamp8(blob.r + randColorOffset());
  blob.g = clamp8(blob.g + randColorOffset());
  blob.b = clamp8(blob.b + randColorOffset());
}

void RandomImagerApp::drawBurst_(Blob& blob) {
  float sinA = std::sin(blob.angle);
  float cosA = std::cos(blob.angle);
  float wobbleSin = std::sin(blob.wobble);

  for (uint16_t i = 0; i < kPixelsPerBurst; ++i) {
    float along = randUnit_() * 2.0f - 1.0f;
    along = std::copysign(std::pow(std::abs(along), 0.35f), along);
    float across = randUnit_() * 2.0f - 1.0f;
    across = std::copysign(std::pow(std::abs(across), 0.6f), across);

    float majorScale = blob.major * (0.68f + 0.32f * wobbleSin);
    float minorScale = blob.minor * (0.7f + 0.24f * std::cos(blob.wobble + along * 2.0f));

    float localX = along * majorScale;
    float localY = across * minorScale;

    float shear = (randUnit_() - 0.5f) * 0.35f * majorScale;
    localY += shear * along;

    float jitter = 2.5f + 4.0f * randUnit_();
    localX += (randUnit_() - 0.5f) * jitter;
    localY += (randUnit_() - 0.5f) * jitter;

    float dx = localX * cosA - localY * sinA;
    float dy = localX * sinA + localY * cosA;

    int16_t px = static_cast<int16_t>(std::round(blob.x + dx));
    int16_t py = static_cast<int16_t>(std::round(blob.y + dy));
    if (px < 0 || px >= TFT_W || py < 0 || py >= TFT_H) {
      continue;
    }

    float majorNorm = std::abs(localX) / (majorScale + 0.01f);
    float minorNorm = std::abs(localY) / (minorScale + 0.01f);
    float dist = std::min(1.0f, std::sqrt(0.62f * majorNorm * majorNorm + 1.38f * minorNorm * minorNorm));
    float highlight = (1.0f - dist);
    float chromaBoost = 0.55f + 0.5f * (1.0f - dist * dist);

    auto addVariation = [&](uint8_t base) -> uint8_t {
      int variation = static_cast<int>((randUnit_() - 0.5f) * 155.0f * highlight);
      int value = static_cast<int>(base * chromaBoost) + variation;
      return clamp8(value);
    };

    uint8_t r = addVariation(blob.r);
    uint8_t g = addVariation(blob.g);
    uint8_t b = addVariation(blob.b);

    // Occasional bright edge slashes introduce focal hints.
    if (randUnit_() < 0.035f) {
      r = clamp8(r + 60);
      g = clamp8(g + 40);
      b = clamp8(b + 70);
    }

    Palette::apply(palette_mode_, r, g, b, r, g, b);

    tft.drawPixel(px, py, pack565_(r, g, b));
  }
}

void RandomImagerApp::clearCanvas_() {
  tft.fillScreen(TFT_BLACK);
}

void RandomImagerApp::init() {
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

void RandomImagerApp::tick(uint32_t delta_ms) {
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

void RandomImagerApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      // Recolour existing blobs.
      for (auto& blob : blobs_) {
        blob.r = static_cast<uint8_t>(randInt_(256));
        blob.g = static_cast<uint8_t>(randInt_(256));
        blob.b = static_cast<uint8_t>(randInt_(256));
        blob.angle = randUnit_() * kTwoPi;
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

void RandomImagerApp::shutdown() {
  // No persistent state.
}

void RandomImagerApp::nextPalette_() {
  palette_mode_ = Palette::nextMode(palette_mode_);
}

void RandomImagerApp::showStatus_(const String& msg) {
  int16_t textY = static_cast<int16_t>((TFT_H - TextRenderer::lineHeight()) / 2);
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, msg, TFT_WHITE, TFT_BLACK);
  pause_until_ = millis() + 1000;
  time_accum_ = 0;
}

const char* RandomImagerApp::paletteName_() const {
  return Palette::modeName(palette_mode_);
}
