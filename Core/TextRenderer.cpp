#include "TextRenderer.h"

#include <LittleFS.h>
#include "Gfx.h"

namespace TextRenderer {
namespace {

constexpr int8_t kOutlineOffsets[][2] = {
    {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

bool fontLoaded = false;
bool fontAttempted = false;
int16_t cachedLineHeight = 19;  // Default fallback (15 ascent + 4 descent)
int16_t cachedAscent = 15;
int16_t cachedDescent = 4;
#ifdef ARDUINO_ARCH_ESP32
#include <esp32-hal-psram.h>
#endif

uint8_t* loadedFontData = nullptr;
constexpr const char* kHelperFontBase = "/system/fontsmall";
constexpr const char* kHelperFontFile = "/system/fontsmall.vlw";
bool helperFontChecked = false;
bool helperFontAvailable = false;

struct HelperFontState {
  uint8_t prevDatum = TL_DATUM;
  uint16_t prevPadding = 0;
  bool restoreMain = false;
  bool helperLoaded = false;
};

bool helperFontExists() {
  if (!helperFontChecked) {
    helperFontChecked = true;
    helperFontAvailable = LittleFS.exists(kHelperFontFile);
  }
  return helperFontAvailable;
}

HelperFontState beginHelperFont() {
  HelperFontState state;
  state.prevDatum = tft.getTextDatum();
  state.prevPadding = tft.getTextPadding();
  state.restoreMain = fontLoaded && (loadedFontData != nullptr);

  if (state.restoreMain) {
    tft.unloadFont();
  }

  if (helperFontExists()) {
    tft.loadFont(kHelperFontBase, LittleFS);
    tft.setTextWrap(false);
    state.helperLoaded = true;
  }

  if (!state.helperLoaded) {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextWrap(false);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  return state;
}

void endHelperFont(const HelperFontState& state) {
  if (state.helperLoaded) {
    tft.unloadFont();
  }
  if (state.restoreMain && loadedFontData) {
    tft.loadFont(loadedFontData);
    tft.setTextWrap(false);
  } else if (!state.helperLoaded) {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextWrap(false);
  }
  tft.setTextDatum(state.prevDatum);
  tft.setTextPadding(state.prevPadding);
}

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
  uint8_t* fontData = nullptr;
  #ifdef ARDUINO_ARCH_ESP32
    if (psramFound()) {
      fontData = (uint8_t*)ps_malloc(fontFileSize);
    }
  #endif
  if (!fontData) {
    fontData = (uint8_t*)malloc(fontFileSize);
  }
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
    "/system/fonts/freesansbold18pt.vlw"
  };

  fontAttempted = true;
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
  fontAttempted = false;

  #ifdef USB_DEBUG
    Serial.println("[TextRenderer] Font unloaded");
  #endif
}

void ensureLoaded() {
  #ifdef USB_DEBUG
    Serial.printf("[TextRenderer] ensureLoaded() called, fontLoaded=%d\n", fontLoaded);
  #endif

  // Check if we have font data AND TFT is using a loaded font (not a built-in font)
  // Built-in fonts (1-8) don't use loadedFontData
  // If TFT is on a built-in font (like Font 4), we need to reload our custom font
  if (fontLoaded && loadedFontData) {
    // Verify TFT is actually using our loaded font by checking if a load is active
    // We do this by calling loadFont again - it's a no-op if already loaded
    tft.loadFont(loadedFontData);
    tft.setTextWrap(false);
    #ifdef USB_DEBUG
      Serial.println("[TextRenderer] Font data present, reloaded to TFT");
    #endif
    return;
  }

  // Font is not loaded - force reload
  #ifdef USB_DEBUG
    Serial.println("[TextRenderer] Font not loaded, forcing reload");
  #endif

  // Reset state
  fontLoaded = false;
  fontAttempted = false;
  if (loadedFontData) {
    free(loadedFontData);
    loadedFontData = nullptr;
  }

  // Reload
  begin();
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

int16_t helperLineHeight() {
  ensureFont();
  HelperFontState state = beginHelperFont();
  int16_t height = tft.fontHeight();
  if (height <= 0) height = 10;
  endHelperFont(state);
  return height;
}

void drawHelperCentered(int16_t yTop, const String& text, uint16_t fgColor, uint16_t bgColor) {
  ensureFont();
  HelperFontState state = beginHelperFont();
  tft.setTextColor(fgColor, bgColor);
  int16_t width = tft.textWidth(text);
  int16_t x = (tft.width() - width) / 2;
  if (x < 0) x = 0;
  tft.drawString(text, x, yTop);
  endHelperFont(state);
}

}  // namespace TextRenderer
