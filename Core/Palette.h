#pragma once

#include <stdint.h>
#include <esp_random.h>

namespace Palette {

constexpr uint8_t kMinComponent = 48;

inline uint8_t rand8() {
  return static_cast<uint8_t>(esp_random() & 0xFF);
}

inline uint8_t ensureMin(uint8_t value) {
  if (value >= kMinComponent) {
    return value;
  }
  uint16_t span = static_cast<uint16_t>(256 - kMinComponent);
  if (span == 0) {
    return 255;
  }
  uint8_t jitter = static_cast<uint8_t>(esp_random() % span);
  return static_cast<uint8_t>(kMinComponent + jitter);
}

inline void apply(uint8_t mode, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                  uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
  switch (mode) {
    case 1: { // Rot
      out_r = ensureMin(base_r);
      out_g = 0;
      out_b = 0;
      break;
    }
    case 2: { // Grün
      out_r = 0;
      out_g = ensureMin(base_g);
      out_b = 0;
      break;
    }
    case 3: { // Blau
      out_r = 0;
      out_g = 0;
      out_b = ensureMin(base_b);
      break;
    }
    case 4: { // Graustufen
      uint8_t avg = static_cast<uint8_t>((static_cast<uint16_t>(base_r) + base_g + base_b) / 3);
      avg = ensureMin(avg);
      out_r = out_g = out_b = avg;
      break;
    }
    case 5: { // Bunt
      out_r = rand8();
      out_g = rand8();
      out_b = rand8();
      break;
    }
    default: { // Alle Farben
      out_r = base_r;
      out_g = base_g;
      out_b = base_b;
      break;
    }
  }
}

inline uint8_t nextMode(uint8_t current) {
  return static_cast<uint8_t>((current + 1) % 6);
}

inline const char* modeName(uint8_t mode) {
  switch (mode) {
    case 1: return "Rot";
    case 2: return "Grün";
    case 3: return "Blau";
    case 4: return "Graustufen";
    case 5: return "Bunt";
    default: return "Alle Farben";
  }
}

}  // namespace Palette

