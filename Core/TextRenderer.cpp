#include "TextRenderer.h"

#include <LittleFS.h>
#include "Gfx.h"

namespace TextRenderer {
namespace {

constexpr int8_t kOutlineOffsets[][2] = {
    {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

bool fontLoaded = false;
int16_t cachedLineHeight = 19;  // Default fallback (15 ascent + 4 descent)
int16_t cachedAscent = 15;
int16_t cachedDescent = 4;

void ensureFont() {
  if (!fontLoaded) {
    begin();
  }
}

}  // namespace

void begin() {
  if (fontLoaded) return;

  // Load font from LittleFS into RAM
  // Note: TFT_eSPI cannot read directly from LittleFS files,
  // so we load the entire font into RAM (~15 KB)
  if (LittleFS.exists("/system/font.vlw")) {
    File fontFile = LittleFS.open("/system/font.vlw", "r");
    if (fontFile) {
      size_t fontFileSize = fontFile.size();
      uint8_t* fontData = (uint8_t*)malloc(fontFileSize);
      if (fontData) {
        size_t bytesRead = fontFile.readBytes((char*)fontData, fontFileSize);
        if (bytesRead == fontFileSize) {
          tft.loadFont(fontData);
          // Note: fontData is intentionally NOT freed - TFT_eSPI uses it
        } else {
          free(fontData);
        }
      }
      fontFile.close();
    }
  }
  // If font loading failed, TFT_eSPI will use its default font

  tft.setTextWrap(false);

  // Cache font metrics
  cachedLineHeight = tft.fontHeight();
  // TFT_eSPI doesn't provide direct ascent/descent, estimate from height
  cachedAscent = (cachedLineHeight * 15) / 19;  // ~79% of height
  cachedDescent = cachedLineHeight - cachedAscent;

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
  return cachedAscent;
}

int16_t descent() {
  ensureFont();
  return cachedDescent;
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
