#include "TextRenderer.h"

#include "BoardOSFont.h"
#include "Gfx.h"

namespace TextRenderer {
namespace {

constexpr int8_t kOutlineOffsets[][2] = {
    {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

bool fontLoaded = false;
int16_t cachedLineHeight = BoardOSFont::DejaVuSansBold20Ascent + BoardOSFont::DejaVuSansBold20Descent;

void ensureFont() {
  if (!fontLoaded) {
    begin();
  }
}

}  // namespace

void begin() {
  if (fontLoaded) return;
  tft.loadFont(BoardOSFont::DejaVuSansBold20);
  tft.setTextWrap(false);
  cachedLineHeight = tft.fontHeight();
  fontLoaded = true;
}

void end() {
  if (!fontLoaded) return;
  tft.unloadFont();
  fontLoaded = false;
}

int16_t lineHeight() {
  ensureFont();
  return cachedLineHeight;
}

int16_t ascent() {
  ensureFont();
  return BoardOSFont::DejaVuSansBold20Ascent;
}

int16_t descent() {
  ensureFont();
  return BoardOSFont::DejaVuSansBold20Descent;
}

int16_t measure(const String& text) {
  ensureFont();
  return tft.textWidth(text);
}

void draw(int16_t x, int16_t yTop, const String& text, uint16_t fgColor, uint16_t outlineColor) {
  ensureFont();
  if (text.isEmpty()) return;

  uint8_t previousDatum = tft.getTextDatum();
  uint16_t previousPadding = tft.getTextPadding();

  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);

  if (outlineColor != fgColor) {
    tft.setTextColor(outlineColor, TFT_TRANSPARENT);
    for (const auto& off : kOutlineOffsets) {
      tft.drawString(text, x + off[0], yTop + off[1]);
    }
  }

  tft.setTextColor(fgColor, TFT_TRANSPARENT);
  tft.drawString(text, x, yTop);

  tft.setTextDatum(previousDatum);
  tft.setTextPadding(previousPadding);
}

void drawCentered(int16_t yTop, const String& text, uint16_t fgColor, uint16_t outlineColor) {
  ensureFont();
  int16_t x = (tft.width() - measure(text)) / 2;
  if (x < 0) x = 0;
  draw(x, yTop, text, fgColor, outlineColor);
}

}  // namespace TextRenderer
