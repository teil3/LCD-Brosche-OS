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
uint8_t* loadedFontData = nullptr;

bool loadFontFromFile(const char* path) {
  if (!path || !path[0]) return false;
  if (!LittleFS.exists(path)) {
    #ifdef USB_DEBUG
      Serial.printf("[TextRenderer] Font path missing: %s\n", path);
    #endif
    return false;
  }
  File fontFile = LittleFS.open(path, "r");
  if (!fontFile) {
    #ifdef USB_DEBUG
      Serial.printf("[TextRenderer] Failed to open %s\n", path);
    #endif
    return false;
  }
  size_t fontFileSize = fontFile.size();
  if (fontFileSize == 0) {
    fontFile.close();
    return false;
  }
  #ifdef USB_DEBUG
    Serial.printf("[TextRenderer] Loading font %s, size=%lu bytes\n",
                  path, static_cast<unsigned long>(fontFileSize));
  #endif
  uint8_t* fontData = (uint8_t*)malloc(fontFileSize);
  if (!fontData) {
    fontFile.close();
    #ifdef USB_DEBUG
      Serial.println("[TextRenderer] Font malloc failed");
    #endif
    return false;
  }
  size_t bytesRead = fontFile.readBytes((char*)fontData, fontFileSize);
  fontFile.close();
  if (bytesRead != fontFileSize) {
    free(fontData);
    #ifdef USB_DEBUG
      Serial.println("[TextRenderer] Font read failed");
    #endif
    return false;
  }
  tft.loadFont(fontData);
  loadedFontData = fontData;
  return true;
}

void ensureFont() {
  if (!fontLoaded) {
    begin();
  }
}

}  // namespace

void begin() {
  #ifdef USB_DEBUG
    Serial.printf("[TextRenderer] begin() called, fontLoaded=%d\n", fontLoaded);
  #endif

  if (fontLoaded) {
    #ifdef USB_DEBUG
      Serial.println("[TextRenderer] Font already loaded, skipping");
    #endif
    return;
  }

  const char* kFallbackFonts[] = {
    "/system/font.vlw",
    "/system/fonts/FreeSansBold18pt.vlw",
    "/system/fonts/freesansbold18pt.vlw",
    "/system/fonts/FreeSans18pt.vlw",
    "/system/fonts/freesans18pt.vlw",
    "/system/fonts/FreeSansBold24pt.vlw",
    "/system/fonts/freesansbold24pt.vlw"
  };

  fontLoaded = false;
  for (const char* candidate : kFallbackFonts) {
    if (loadFontFromFile(candidate)) {
      fontLoaded = true;
      break;
    }
  }
  if (!fontLoaded) {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextWrap(false);
  }

  tft.setTextWrap(false);

  // Cache font metrics
  cachedLineHeight = tft.fontHeight();
  if (cachedLineHeight <= 0) cachedLineHeight = 19;
  cachedAscent = (cachedLineHeight * 15) / 19;
  cachedDescent = cachedLineHeight - cachedAscent;

  #ifdef USB_DEBUG
    Serial.printf("[TextRenderer] Font metrics: height=%d\n", cachedLineHeight);
  #endif
}

void end() {
  #ifdef USB_DEBUG
    Serial.printf("[TextRenderer] end() called, fontLoaded=%d\n", fontLoaded);
  #endif

  if (!fontLoaded) {
    #ifdef USB_DEBUG
      Serial.println("[TextRenderer] Font not loaded, skipping");
    #endif
    return;
  }

  tft.unloadFont();
  if (loadedFontData) {
    free(loadedFontData);
    loadedFontData = nullptr;
  }
  fontLoaded = false;

  #ifdef USB_DEBUG
    Serial.println("[TextRenderer] Font unloaded");
  #endif
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
