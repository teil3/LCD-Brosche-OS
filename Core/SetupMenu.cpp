#include "SetupMenu.h"

#include <array>

#include "Config.h"
#include "Gfx.h"
#include "TextRenderer.h"

namespace {
constexpr std::array<const char*, static_cast<size_t>(SetupMenu::Item::Count)> kLabels = {
  "USB/BLE-Transfer",
  "SD-Transfer",
  "Quellen-Wahl",
  "Exit"
};

inline uint8_t clampIndex(uint8_t idx) {
  const uint8_t count = static_cast<uint8_t>(SetupMenu::Item::Count);
  return (idx < count) ? idx : (count - 1);
}
}  // namespace

void SetupMenu::show() {
  visible_ = true;
  selected_ = 0;
  dirty_ = true;
  statusText_.clear();
  statusUntil_ = 0;
}

void SetupMenu::hide() {
  if (!visible_) return;
  visible_ = false;
  dirty_ = false;
  statusText_.clear();
  statusUntil_ = 0;
}

SetupMenu::Item SetupMenu::currentItem() const {
  return static_cast<Item>(clampIndex(selected_));
}

void SetupMenu::next() {
  if (!visible_) return;
  selected_ = (selected_ + 1) % static_cast<uint8_t>(Item::Count);
  dirty_ = true;
}

void SetupMenu::draw(bool force) {
  if (!visible_) return;
  if (!force && !dirty_) return;
  dirty_ = false;

  tft.fillScreen(TFT_BLACK);

  const int16_t line = TextRenderer::lineHeight();
  const int16_t spacing = 10;
  int16_t top = 24;
  TextRenderer::drawCentered(top, "Setup", TFT_WHITE, TFT_BLACK);

  for (size_t i = 0; i < kLabels.size(); ++i) {
    int16_t y = top + line + spacing + static_cast<int16_t>(i) * (line + spacing);
    String label(kLabels[i]);
    uint16_t color = TFT_DARKGREY;
    if (static_cast<uint8_t>(i) == selected_) {
      label = String("> ") + label;
      color = TFT_WHITE;
    }
    TextRenderer::drawCentered(y, label, color, TFT_BLACK);
  }

  bool statusActive = false;
  if (!statusText_.isEmpty()) {
    uint32_t now = millis();
    if (now < statusUntil_) {
      statusActive = true;
    } else {
      statusText_.clear();
      statusUntil_ = 0;
      dirty_ = true;
    }
  }

  const int16_t helperLine = TextRenderer::helperLineHeight();
  const int16_t hintY = TFT_H - (helperLine * 2) - 8 - 20;
  if (statusActive) {
    TextRenderer::drawHelperCentered(hintY, statusText_, TFT_WHITE, TFT_BLACK);
    TextRenderer::drawHelperCentered(hintY + helperLine + 2,
                                    "BTN2 kurz/lang: Menü", TFT_WHITE, TFT_BLACK);
  } else {
    TextRenderer::drawHelperCentered(hintY, "BTN2 kurz: Wechseln", TFT_WHITE, TFT_BLACK);
    TextRenderer::drawHelperCentered(hintY + helperLine + 2,
                                    "BTN2 lang: Öffnen", TFT_WHITE, TFT_BLACK);
  }
}

void SetupMenu::showStatus(const String& text, uint32_t duration_ms) {
  statusText_ = text;
  statusUntil_ = millis() + duration_ms;
  dirty_ = true;
}
