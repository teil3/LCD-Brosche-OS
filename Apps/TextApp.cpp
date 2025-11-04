#include "TextApp.h"

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Storage.h"
#include "Core/TextRenderer.h"
#include <LittleFS.h>
#include <algorithm>
#include <ctype.h>
#include <vector>

#ifndef LOAD_GFXFF
#define LOAD_GFXFF
#endif

extern const GFXfont FreeSans18pt7b;
extern const GFXfont FreeSansBold18pt7b;
extern const GFXfont FreeSansBold24pt7b;
extern const GFXfont FreeSerif18pt7b;

namespace {
constexpr const char* kConfigPath = "/textapp.cfg";
constexpr uint32_t kCycleSpeeds[] = {200, 500, 1000, 2000, 3000};
constexpr uint8_t kCycleSpeedCount = sizeof(kCycleSpeeds) / sizeof(kCycleSpeeds[0]);

struct FontMapEntry {
  const char* name;
  const GFXfont* font;
};

constexpr FontMapEntry kFontMap[] = {
    {"FreeSans18pt", &FreeSans18pt7b},
    {"FreeSansBold18pt", &FreeSansBold18pt7b},
    {"FreeSansBold24pt", &FreeSansBold24pt7b},
    {"FreeSerif18pt", &FreeSerif18pt7b},
};

constexpr size_t kFontMapCount = sizeof(kFontMap) / sizeof(kFontMap[0]);

const FontMapEntry* lookupFont(const String& name) {
  for (size_t i = 0; i < kFontMapCount; ++i) {
    if (name.equalsIgnoreCase(kFontMap[i].name)) {
      return &kFontMap[i];
    }
  }
  return nullptr;
}
}  // namespace

void TextApp::init() {
  timeAccum_ = 0;
  bigLetterIndex_ = 0;
  bigWordIndex_ = 0;
  wordsDirty_ = true;
  words_.clear();
  pauseUntil_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;

  TextRenderer::end();
  fontExplicit_ = false;
  currentFont_ = nullptr;

  loadConfig_();
  chooseFont_();
  applyFont_();
  rebuildWords_();

  tft.fillScreen(bgColor_);
}

void TextApp::tick(uint32_t delta_ms) {
  if (pauseUntil_) {
    uint32_t now = millis();
    if (now < pauseUntil_) {
      return;
    }
    pauseUntil_ = 0;
    needsRedraw_ = true;
  }

  if (mode_ == DisplayMode::BigLetters) {
    timeAccum_ += delta_ms;
    while (timeAccum_ >= letterSpeed_) {
      timeAccum_ -= letterSpeed_;
      bigLetterIndex_++;
      if (bigLetterIndex_ >= text_.length()) {
        bigLetterIndex_ = 0;
      }
      needsRedraw_ = true;
    }
  } else if (mode_ == DisplayMode::BigWords) {
    if (wordsDirty_) {
      rebuildWords_();
    }
    if (words_.empty()) {
      return;
    }
    timeAccum_ += delta_ms;
    while (timeAccum_ >= letterSpeed_) {
      timeAccum_ -= letterSpeed_;
      bigWordIndex_++;
      if (bigWordIndex_ >= words_.size()) {
        bigWordIndex_ = 0;
      }
      needsRedraw_ = true;
    }
  }
}

void TextApp::draw() {
  if (!needsRedraw_) return;

  tft.fillScreen(bgColor_);

  switch (mode_) {
    case DisplayMode::SingleLine:
      drawSingleLine_();
      break;
    case DisplayMode::MultiLine:
      drawMultiLine_();
      break;
    case DisplayMode::BigWords:
      drawBigWord_();
      break;
    case DisplayMode::BigLetters:
      drawBigLetter_();
      break;
  }

  needsRedraw_ = false;
}

void TextApp::shutdown() {
  TextRenderer::begin();
}

void TextApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2) return;

  switch (e) {
    case BtnEvent::Single:
      nextMode_();
      break;
    case BtnEvent::Double:
      cycleSpeed_();
      break;
    case BtnEvent::Long:
      reloadConfig_();
      break;
    default:
      break;
  }
}

void TextApp::loadConfig_() {
  if (!LittleFS.exists(kConfigPath)) {
    return;
  }

  File f = LittleFS.open(kConfigPath, "r");
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith("#")) continue;
    parseConfigLine_(line);
  }
  f.close();
}

void TextApp::parseConfigLine_(const String& line) {
  int eqPos = line.indexOf('=');
  if (eqPos < 0) return;

  String key = line.substring(0, eqPos);
  String value = line.substring(eqPos + 1);
  key.trim();
  value.trim();

  if (key.equalsIgnoreCase("MODE")) {
    mode_ = stringToMode_(value);
  } else if (key.equalsIgnoreCase("TEXT")) {
    text_ = value;
    text_.replace("|", "\n");
    wordsDirty_ = true;
  } else if (key.equalsIgnoreCase("COLOR")) {
    color_ = parseColor_(value);
  } else if (key.equalsIgnoreCase("BG_COLOR") || key.equalsIgnoreCase("BGCOLOR")) {
    bgColor_ = parseColor_(value);
  } else if (key.equalsIgnoreCase("SIZE")) {
    int sz = value.toInt();
    if (sz < 1) sz = 1;
    if (sz > 10) sz = 10;
    textSize_ = static_cast<uint8_t>(sz);
    fontExplicit_ = false;
  } else if (key.equalsIgnoreCase("FONT")) {
    const FontMapEntry* entry = lookupFont(value);
    if (entry) {
      currentFont_ = entry->font;
      fontName_ = entry->name;
      fontExplicit_ = true;
    }
  } else if (key.equalsIgnoreCase("LETTER_SPEED") || key.equalsIgnoreCase("WORD_SPEED") ||
             key.equalsIgnoreCase("CYCLE_SPEED")) {
    letterSpeed_ = value.toInt();
    if (letterSpeed_ < 10) letterSpeed_ = 10;
    if (letterSpeed_ > 10000) letterSpeed_ = 10000;
  } else if (key.equalsIgnoreCase("WORD_SPEED")) {
    letterSpeed_ = value.toInt();
    if (letterSpeed_ < 10) letterSpeed_ = 10;
    if (letterSpeed_ > 10000) letterSpeed_ = 10000;
  } else if (key.equalsIgnoreCase("SPEED")) {
    uint32_t spd = value.toInt();
    if (spd >= 10 && spd <= 10000) {
      letterSpeed_ = spd;
    }
  }
}

TextApp::DisplayMode TextApp::stringToMode_(const String& str) const {
  if (str.equalsIgnoreCase("single_line")) return DisplayMode::SingleLine;
  if (str.equalsIgnoreCase("multi_line")) return DisplayMode::MultiLine;
  if (str.equalsIgnoreCase("big_words") ||
      str.equalsIgnoreCase("words"))
    return DisplayMode::BigWords;
  if (str.equalsIgnoreCase("big_letters")) return DisplayMode::BigLetters;
  return DisplayMode::SingleLine;
}

uint16_t TextApp::parseColor_(const String& str) const {
  String hex = str;
  if (hex.startsWith("0x") || hex.startsWith("0X")) {
    hex = hex.substring(2);
  }
  unsigned long val = strtoul(hex.c_str(), nullptr, 16);
  return static_cast<uint16_t>(val & 0xFFFF);
}

void TextApp::drawSingleLine_() {
  tft.setTextColor(color_, bgColor_);
  chooseFont_();
  setCanvasFont_();

  int16_t h = tft.fontHeight();
  if (h <= 0) h = 16;
  int16_t w = tft.textWidth(text_);
  int16_t x = (TFT_W - w) / 2;
  if (x < 0) x = 0;
  int16_t baseline = (TFT_H - h) / 2 + h - 1;

  tft.setCursor(x, baseline);
  tft.print(text_);
}

void TextApp::drawMultiLine_() {
  tft.setTextColor(color_, bgColor_);
  chooseFont_();
  setCanvasFont_();

  std::vector<String> lines;
  String remaining = text_;
  while (true) {
    int nl = remaining.indexOf('\n');
    if (nl < 0) {
      lines.push_back(remaining);
      break;
    }
    lines.push_back(remaining.substring(0, nl));
    remaining = remaining.substring(nl + 1);
  }

  int16_t lineHeight = tft.fontHeight();
  if (lineHeight <= 0) lineHeight = 16;
  int16_t totalHeight = lines.size() * lineHeight + (lines.size() ? (lines.size() - 1) * 4 : 0);
  int16_t y = (TFT_H - totalHeight) / 2;
  if (y < 0) y = 0;

  for (const String& line : lines) {
    if (!line.isEmpty()) {
      int16_t w = tft.textWidth(line);
      int16_t x = (TFT_W - w) / 2;
      if (x < 0) x = 0;
      int16_t baseline = y + lineHeight - 1;
      tft.setCursor(x, baseline);
      tft.print(line);
    }
    y += lineHeight + 4;
    if (y >= TFT_H) break;
  }
}

void TextApp::drawBigWord_() {
  if (wordsDirty_) {
    rebuildWords_();
  }
  if (words_.empty()) return;
  if (bigWordIndex_ >= words_.size()) bigWordIndex_ = 0;

  const String& word = words_[bigWordIndex_];

  if (!currentFont_) {
    chooseFont_();
  }

  const GFXfont* baseFont = currentFont_ ? currentFont_ : &FreeSansBold18pt7b;
  const GFXfont* candidates[] = {
      baseFont,
      &FreeSansBold24pt7b,
      &FreeSansBold18pt7b,
      &FreeSans18pt7b,
  };

  TFT_eSprite wordSprite(&tft);
  wordSprite.setColorDepth(16);
  wordSprite.setTextWrap(false);
  wordSprite.setTextColor(color_, bgColor_);

  const GFXfont* font = nullptr;
  int16_t srcWidth = 1;
  int16_t srcHeight = 32;

  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    const GFXfont* candidate = candidates[i];
    if (!candidate) continue;
    bool alreadyUsed = false;
    for (size_t j = 0; j < i; ++j) {
      if (candidate == candidates[j]) {
        alreadyUsed = true;
        break;
      }
    }
    if (alreadyUsed) continue;

    wordSprite.setFreeFont(candidate);
    int16_t h = wordSprite.fontHeight();
    if (h <= 0) h = 32;
    int16_t w = wordSprite.textWidth(word.c_str());
    if (w <= 0) w = 1;

    font = candidate;
    srcHeight = h;
    srcWidth = w;
    if (w <= TFT_W) {
      break;
    }
  }

  if (!font) {
    font = &FreeSansBold18pt7b;
    wordSprite.setFreeFont(font);
    srcHeight = wordSprite.fontHeight();
    if (srcHeight <= 0) srcHeight = 24;
    srcWidth = wordSprite.textWidth(word.c_str());
    if (srcWidth <= 0) srcWidth = 1;
  }

  if (!wordSprite.createSprite(srcWidth, srcHeight)) {
    // Fallback to direct draw if sprite allocation fails
    tft.setTextColor(color_, bgColor_);
    tft.setFreeFont(font);
    tft.setTextWrap(false);

    int16_t h = tft.fontHeight();
    if (h <= 0) h = srcHeight;
    int16_t w = tft.textWidth(word.c_str());
    if (w <= 0) w = srcWidth;
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;

    tft.setCursor(x, baseline);
    tft.print(word);
    return;
  }

  wordSprite.fillSprite(bgColor_);
  wordSprite.setCursor(0, srcHeight - 1);
  wordSprite.print(word);

  int32_t minX = srcWidth, minY = srcHeight;
  int32_t maxX = -1, maxY = -1;
  for (int32_t y = 0; y < srcHeight; ++y) {
    for (int32_t x = 0; x < srcWidth; ++x) {
      if (wordSprite.readPixel(x, y) != bgColor_) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX < minX || maxY < minY) {
    wordSprite.deleteSprite();
    return;
  }

  int32_t contentW = maxX - minX + 1;
  int32_t contentH = maxY - minY + 1;

  int32_t destX = (TFT_W - contentW) / 2;
  int32_t destY = (TFT_H - contentH) / 2;
  if (destX < 0) destX = 0;
  if (destY < 0) destY = 0;

  for (int32_t y = 0; y < contentH; ++y) {
    int32_t srcY = minY + y;
    for (int32_t x = 0; x < contentW; ++x) {
      int32_t srcX = minX + x;
      uint16_t pixel = wordSprite.readPixel(srcX, srcY);
      if (pixel != bgColor_) {
        int32_t drawX = destX + x;
        int32_t drawY = destY + y;
        if (drawX >= 0 && drawX < TFT_W && drawY >= 0 && drawY < TFT_H) {
          tft.drawPixel(drawX, drawY, pixel);
        }
      }
    }
  }

  wordSprite.deleteSprite();
}

void TextApp::drawBigLetter_() {
  if (text_.isEmpty()) return;
  if (bigLetterIndex_ >= text_.length()) bigLetterIndex_ = 0;

  char symbol = text_[bigLetterIndex_];
  if (symbol == '\n' || symbol == '\r') {
    return;
  }

  String letter = String(symbol);

  if (!currentFont_) {
    chooseFont_();
  }
  const GFXfont* font = currentFont_ ? currentFont_ : &FreeSansBold24pt7b;

  TFT_eSprite letterSprite(&tft);
  letterSprite.setColorDepth(16);
  letterSprite.setFreeFont(font);
  letterSprite.setTextWrap(false);
  letterSprite.setTextColor(color_, bgColor_);

  int16_t srcHeight = letterSprite.fontHeight();
  if (srcHeight <= 0) srcHeight = 32;
  int16_t srcWidth = letterSprite.textWidth(letter.c_str());
  if (srcWidth <= 0) srcWidth = 1;

  if (!letterSprite.createSprite(srcWidth, srcHeight)) {
    // Fallback: draw without scaling if sprite allocation fails
    tft.setTextColor(color_, bgColor_);
    tft.setFreeFont(font);
    tft.setTextWrap(false);

    int16_t h = tft.fontHeight();
    if (h <= 0) h = 32;
    int16_t w = tft.textWidth(letter.c_str());
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;

    tft.setCursor(x, baseline);
    tft.print(letter);
    return;
  }

  letterSprite.fillSprite(bgColor_);
  letterSprite.setCursor(0, srcHeight - 1);
  letterSprite.print(letter);

  // Compute tight bounding box to keep glyph centred after baseline offset
  int32_t minX = srcWidth, minY = srcHeight;
  int32_t maxX = -1, maxY = -1;
  for (int32_t y = 0; y < srcHeight; ++y) {
    for (int32_t x = 0; x < srcWidth; ++x) {
      if (letterSprite.readPixel(x, y) != bgColor_) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX < minX || maxY < minY) {
    letterSprite.deleteSprite();
    return;
  }

  int32_t contentW = maxX - minX + 1;
  int32_t contentH = maxY - minY + 1;

  const float targetHeight = TFT_H * 0.7f;
  float scale = targetHeight / static_cast<float>(contentH);
  if (scale < 1.0f) scale = 1.0f;

  const float maxScaleWidth = static_cast<float>(TFT_W) / static_cast<float>(contentW);
  const float maxScaleHeight = static_cast<float>(TFT_H) / static_cast<float>(contentH);
  scale = std::min(scale, maxScaleWidth);
  scale = std::min(scale, maxScaleHeight);
  if (scale < 1.0f) scale = 1.0f;

  int32_t scaledW = std::max<int32_t>(1, static_cast<int32_t>(contentW * scale));
  int32_t scaledH = std::max<int32_t>(1, static_cast<int32_t>(contentH * scale));
  if (scaledW > TFT_W) scaledW = TFT_W;
  if (scaledH > TFT_H) scaledH = TFT_H;

  int32_t destX = (TFT_W - scaledW) / 2;
  int32_t destY = (TFT_H - scaledH) / 2;
  if (destX < 0) destX = 0;
  if (destY < 0) destY = 0;

  for (int32_t dy = 0; dy < scaledH; ++dy) {
    int32_t srcY = minY + (dy * contentH) / scaledH;
    for (int32_t dx = 0; dx < scaledW; ++dx) {
      int32_t srcX = minX + (dx * contentW) / scaledW;
      uint16_t pixel = letterSprite.readPixel(srcX, srcY);
      if (pixel != bgColor_) {
        int32_t x = destX + dx;
        int32_t y = destY + dy;
        if (x >= 0 && x < TFT_W && y >= 0 && y < TFT_H) {
          tft.drawPixel(x, y, pixel);
        }
      }
    }
  }

  letterSprite.deleteSprite();
}

void TextApp::nextMode_() {
  uint8_t current = static_cast<uint8_t>(mode_);
  current = (current + 1) % 4;
  mode_ = static_cast<DisplayMode>(current);

  bigLetterIndex_ = 0;
  bigWordIndex_ = 0;
  wordsDirty_ = true;
  timeAccum_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;

  if (mode_ == DisplayMode::BigWords) {
    rebuildWords_();
  }

  showStatus_(String("Modus: ") + modeName_());
}

void TextApp::cycleSpeed_() {
  if (mode_ == DisplayMode::BigLetters || mode_ == DisplayMode::BigWords) {
    speedIndex_ = (speedIndex_ + 1) % kCycleSpeedCount;
    letterSpeed_ = kCycleSpeeds[speedIndex_];
    timeAccum_ = 0;
    showStatus_(String("Tempo: ") + String(letterSpeed_) + "ms");
  }
}

void TextApp::reloadConfig_() {
  mode_ = DisplayMode::SingleLine;
  text_ = "Hallo Welt!";
  color_ = 0xFFFF;
  bgColor_ = 0x0000;
  textSize_ = 2;
  fontExplicit_ = false;
  currentFont_ = nullptr;
  fontName_ = "FreeSansBold18pt";
  letterSpeed_ = 1000;

  loadConfig_();
  chooseFont_();
  applyFont_();
  wordsDirty_ = true;
  rebuildWords_();

  bigLetterIndex_ = 0;
  bigWordIndex_ = 0;
  timeAccum_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;

  showStatus_("Config geladen");
}

void TextApp::showStatus_(const String& msg) {
  tft.fillScreen(bgColor_);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextWrap(false);
  tft.setTextColor(TFT_WHITE, bgColor_);

  int16_t h = tft.fontHeight();
  if (h <= 0) h = 24;
  int16_t w = tft.textWidth(msg);
  int16_t x = (TFT_W - w) / 2;
  if (x < 0) x = 0;
  int16_t baseline = (TFT_H - h) / 2 + h - 1;

  tft.setCursor(x, baseline);
  tft.print(msg);

  pauseUntil_ = millis() + 1000;
  needsRedraw_ = true;
}

uint32_t TextApp::currentSpeed_() const {
  if (mode_ == DisplayMode::BigLetters || mode_ == DisplayMode::BigWords) return letterSpeed_;
  return 50;
}

const char* TextApp::modeName_() const {
  switch (mode_) {
    case DisplayMode::SingleLine: return "1 Zeile";
    case DisplayMode::MultiLine: return "Mehrere";
    case DisplayMode::BigWords: return "Woerter";
    case DisplayMode::BigLetters: return "Buchstaben";
  }
  return "?";
}

void TextApp::chooseFont_() {
  if (!fontExplicit_) {
    if (textSize_ >= 4) {
      currentFont_ = &FreeSansBold24pt7b;
      fontName_ = "FreeSansBold24pt";
    } else {
      currentFont_ = &FreeSansBold18pt7b;
      fontName_ = "FreeSansBold18pt";
    }
  }
  if (!currentFont_) {
    currentFont_ = &FreeSansBold18pt7b;
    fontName_ = "FreeSansBold18pt";
  }
}

void TextApp::applyFont_() {
  chooseFont_();
  tft.setFreeFont(currentFont_);
  tft.setTextWrap(false);
}

void TextApp::setCanvasFont_() {
  tft.setFreeFont(currentFont_);
  tft.setTextWrap(false);
}

void TextApp::rebuildWords_() {
  words_.clear();

  if (text_.isEmpty()) {
    bigWordIndex_ = 0;
    wordsDirty_ = false;
    return;
  }

  String current;
  current.reserve(text_.length());

  for (size_t i = 0; i < static_cast<size_t>(text_.length()); ++i) {
    char c = text_[i];
    if (isspace(static_cast<unsigned char>(c))) {
      if (current.length()) {
        words_.push_back(current);
        current = "";
      }
    } else {
      current += c;
    }
  }

  if (current.length()) {
    words_.push_back(current);
  }

  String trimmed = text_;
  trimmed.trim();
  if (words_.empty() && !trimmed.isEmpty()) {
    words_.push_back(trimmed);
  }

  bigWordIndex_ = 0;
  wordsDirty_ = false;
}
