#include "TextApp.h"

#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Storage.h"
#include "Core/TextRenderer.h"
#include <LittleFS.h>
#include <algorithm>
#include <ctype.h>
#include <vector>

namespace {
constexpr const char* kConfigPath = "/textapp.cfg";
constexpr uint32_t kCycleSpeeds[] = {200, 500, 1000, 2000, 3000};
constexpr uint8_t kCycleSpeedCount = sizeof(kCycleSpeeds) / sizeof(kCycleSpeeds[0]);

struct FontMapEntry {
  const char* name;
  const char* path;
};

constexpr FontMapEntry kFontMap[] = {
    {"FreeSans18pt", "system/fonts/freesans18pt"},
    {"FreeSansBold18pt", "system/fonts/freesansbold18pt"},
    {"FreeSansBold24pt", "system/fonts/freesansbold24pt"},
    {"FreeSerif18pt", "system/fonts/freeserif18pt"},
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
  fontLoaded_ = false;
  fontLoadFailed_ = false;
  unloadFont_();

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
    case DisplayMode::TextBlock:
      drawTextBlock_();
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
  unloadFont_();
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
      if (mode_ == DisplayMode::TextBlock) {
        cycleAlignment_();
      }
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
    fontLoadFailed_ = false;
  } else if (key.equalsIgnoreCase("FONT")) {
    String cleaned = value;
    if (cleaned.endsWith(".vlw")) {
      cleaned = cleaned.substring(0, cleaned.length() - 4);
    }
    while (cleaned.startsWith("/")) {
      cleaned.remove(0, 1);
    }
    const FontMapEntry* entry = lookupFont(cleaned);
    if (entry) {
      fontName_ = entry->name;
    } else {
      fontName_ = cleaned;
    }
    fontExplicit_ = true;
    fontLoadFailed_ = false;
  } else if (key.equalsIgnoreCase("ALIGN")) {
    if (value.equalsIgnoreCase("left")) alignment_ = TextAlign::Left;
    else if (value.equalsIgnoreCase("right")) alignment_ = TextAlign::Right;
    else alignment_ = TextAlign::Center;
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
  if (str.equalsIgnoreCase("single_line") ||
      str.equalsIgnoreCase("multi_line") ||
      str.equalsIgnoreCase("text") ||
      str.equalsIgnoreCase("block"))
    return DisplayMode::TextBlock;
  if (str.equalsIgnoreCase("big_words") ||
      str.equalsIgnoreCase("words"))
    return DisplayMode::BigWords;
  if (str.equalsIgnoreCase("big_letters")) return DisplayMode::BigLetters;
  return DisplayMode::TextBlock;
}

uint16_t TextApp::parseColor_(const String& str) const {
  String hex = str;
  if (hex.startsWith("0x") || hex.startsWith("0X")) {
    hex = hex.substring(2);
  }
  unsigned long val = strtoul(hex.c_str(), nullptr, 16);
  return static_cast<uint16_t>(val & 0xFFFF);
}

void TextApp::drawTextBlock_() {
  chooseFont_();
  if (!ensureFontLoaded_()) {
    tft.fillScreen(bgColor_);
    return;
  }

  tft.setTextColor(color_, bgColor_);
  tft.setTextWrap(false);

  std::vector<String> lines;
  lines.reserve(8);
  String remaining = text_;
  if (remaining.isEmpty()) {
    lines.push_back("");
  } else {
    while (true) {
      int nl = remaining.indexOf('\n');
      if (nl < 0) {
        lines.push_back(remaining);
        break;
      }
      lines.push_back(remaining.substring(0, nl));
      remaining = remaining.substring(nl + 1);
    }
  }

  TFT_eSprite blockSprite(&tft);
  blockSprite.setColorDepth(16);
  blockSprite.setTextWrap(false);
  blockSprite.setTextColor(color_, bgColor_);

  if (!loadSpriteFont_(blockSprite, fontName_)) {
    // Fallback: draw directly without centering adjustments
    tft.fillScreen(bgColor_);
    int16_t lineHeight = tft.fontHeight();
    if (lineHeight <= 0) lineHeight = 18;
    int16_t ascent = (lineHeight * 15) / 19;
    int16_t gap = std::max<int16_t>(4, lineHeight / 5);
    int16_t totalHeight = static_cast<int16_t>(lines.size()) * lineHeight;
    if (lines.size() > 1) totalHeight += static_cast<int16_t>(lines.size() - 1) * gap;
    // y is the baseline position of the first line
    int16_t y = (TFT_H - totalHeight) / 2 + ascent;
    for (const String& line : lines) {
      int16_t width = tft.textWidth(line);
      int16_t x = (alignment_ == TextAlign::Left) ? 6 : (alignment_ == TextAlign::Right ? TFT_W - width - 6 : (TFT_W - width) / 2);
      if (x < 6) x = 6;
      if (x + width > TFT_W - 6) x = std::max<int16_t>(6, TFT_W - width - 6);
      tft.setCursor(x, y);
      tft.print(line);
      y += lineHeight + gap;
    }
    blockSprite.unloadFont();
    blockSprite.deleteSprite();
    return;
  }

  int16_t lineHeight = blockSprite.fontHeight();
  if (lineHeight <= 0) lineHeight = 18;
  int16_t ascent = (lineHeight * 15) / 19;
  int16_t gap = std::max<int16_t>(4, lineHeight / 5);

  int16_t maxWidth = 0;
  for (const auto& line : lines) {
    int16_t w = blockSprite.textWidth(line.c_str());
    if (w > maxWidth) maxWidth = w;
  }

  const int16_t margin = 8;
  int16_t spriteWidth = std::min<int16_t>(TFT_W, maxWidth + margin * 2);
  int16_t totalHeight = static_cast<int16_t>(lines.size()) * lineHeight;
  if (lines.size() > 1) totalHeight += static_cast<int16_t>(lines.size() - 1) * gap;
  int16_t spriteHeight = std::min<int16_t>(TFT_H, totalHeight + margin * 2);

  if (!blockSprite.createSprite(spriteWidth, spriteHeight)) {
    blockSprite.unloadFont();
    blockSprite.deleteSprite();
    // Fallback to direct drawing
    ensureFontLoaded_();
    tft.fillScreen(bgColor_);

    // Use middle datum for vertical centering
    tft.setTextDatum(ML_DATUM);

    // Calculate Y position for each line (middle of line)
    int16_t startY = (TFT_H - totalHeight) / 2 + lineHeight / 2;
    int16_t y = startY;

    for (const String& line : lines) {
      int16_t x;
      switch (alignment_) {
        case TextAlign::Left:
          x = 6;
          tft.setTextDatum(ML_DATUM);  // Middle-Left
          break;
        case TextAlign::Center:
          x = TFT_W / 2;
          tft.setTextDatum(MC_DATUM);  // Middle-Center
          break;
        case TextAlign::Right:
          x = TFT_W - 6;
          tft.setTextDatum(MR_DATUM);  // Middle-Right
          break;
      }

      tft.drawString(line, x, y);
      y += lineHeight + gap;
    }
    return;
  }

  blockSprite.fillSprite(bgColor_);
  blockSprite.setTextDatum(BL_DATUM);

  // Draw text starting from top of sprite (only baseline offset)
  // Bounding box calculation will find actual text bounds
  int16_t y = ascent;
  for (const auto& line : lines) {
    int16_t width = blockSprite.textWidth(line.c_str());
    int16_t x;
    switch (alignment_) {
      case TextAlign::Left:
        x = margin;
        break;
      case TextAlign::Center:
        x = (spriteWidth - width) / 2;
        break;
      case TextAlign::Right:
        x = spriteWidth - width - margin;
        break;
    }
    if (x < margin) x = margin;
    if (x + width > spriteWidth - margin) x = std::max<int16_t>(margin, spriteWidth - width - margin);

    blockSprite.drawString(line, x, y);
    y += lineHeight + gap;
    if (y >= spriteHeight) break;
  }

  int32_t minX = spriteWidth, minY = spriteHeight;
  int32_t maxX = -1, maxY = -1;
  for (int32_t yy = 0; yy < spriteHeight; ++yy) {
    for (int32_t xx = 0; xx < spriteWidth; ++xx) {
      if (blockSprite.readPixel(xx, yy) != bgColor_) {
        if (xx < minX) minX = xx;
        if (xx > maxX) maxX = xx;
        if (yy < minY) minY = yy;
        if (yy > maxY) maxY = yy;
      }
    }
  }

  if (maxX < minX || maxY < minY) {
    blockSprite.unloadFont();
    blockSprite.deleteSprite();
    return;
  }

  int32_t blockW = maxX - minX + 1;
  int32_t blockH = maxY - minY + 1;

  int32_t destX;
  switch (alignment_) {
    case TextAlign::Left:
      destX = margin;
      break;
    case TextAlign::Center:
      destX = (TFT_W - blockW) / 2;
      break;
    case TextAlign::Right:
      destX = TFT_W - blockW - margin;
      break;
  }
  if (destX < margin) destX = margin;
  if (destX + blockW > TFT_W - margin) destX = std::max<int32_t>(margin, TFT_W - blockW - margin);

  int32_t destY = (TFT_H - blockH) / 2;
  if (destY < margin) destY = margin;
  if (destY + blockH > TFT_H - margin) destY = std::max<int32_t>(margin, TFT_H - blockH - margin);

  for (int32_t dy = 0; dy < blockH; ++dy) {
    int32_t srcY = minY + dy;
    for (int32_t dx = 0; dx < blockW; ++dx) {
      int32_t srcX = minX + dx;
      uint16_t pixel = blockSprite.readPixel(srcX, srcY);
      if (pixel != bgColor_) {
        int32_t xDraw = destX + dx;
        int32_t yDraw = destY + dy;
        if (xDraw >= 0 && xDraw < TFT_W && yDraw >= 0 && yDraw < TFT_H) {
          tft.drawPixel(xDraw, yDraw, pixel);
        }
      }
    }
  }

  blockSprite.unloadFont();
  blockSprite.deleteSprite();
}

void TextApp::drawBigWord_() {
  if (wordsDirty_) {
    rebuildWords_();
  }
  if (words_.empty()) return;
  if (bigWordIndex_ >= words_.size()) bigWordIndex_ = 0;

  const String& word = words_[bigWordIndex_];
  chooseFont_();

  std::vector<String> candidates;
  candidates.reserve(4);
  candidates.push_back(fontName_);
  auto pushIfMissing = [&candidates](const char* name) {
    for (const auto& existing : candidates) {
      if (existing.equalsIgnoreCase(name)) {
        return;
      }
    }
    candidates.emplace_back(name);
  };
  pushIfMissing("FreeSansBold24pt");
  pushIfMissing("FreeSansBold18pt");
  pushIfMissing("FreeSans18pt");

  TFT_eSprite wordSprite(&tft);
  wordSprite.setColorDepth(16);
  wordSprite.setTextWrap(false);
  wordSprite.setTextColor(color_, bgColor_);

  String selectedFont = fontName_;
  int16_t textWidth = 1;
  int16_t textHeight = 32;
  bool spriteFontLoaded = false;

  for (const auto& candidate : candidates) {
    if (!loadSpriteFont_(wordSprite, candidate)) {
      continue;
    }
    spriteFontLoaded = true;
    selectedFont = candidate;
    int16_t h = wordSprite.fontHeight();
    if (h <= 0) h = 32;
    int16_t w = wordSprite.textWidth(word.c_str());
    if (w <= 0) w = 1;
    textHeight = h;
    textWidth = w;
    if (w <= TFT_W) {
      break;
    }
    wordSprite.unloadFont();
    spriteFontLoaded = false;
  }

  if (!spriteFontLoaded) {
    ensureFontLoaded_();
    tft.setTextColor(color_, bgColor_);
    tft.setTextWrap(false);
    int16_t h = tft.fontHeight();
    if (h <= 0) h = textHeight;
    int16_t w = tft.textWidth(word);
    if (w <= 0) w = textWidth;
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;
    tft.setCursor(x, baseline);
    tft.print(word);
    return;
  }

  int16_t topMargin = std::max<int16_t>(4, textHeight / 6);
  int16_t bottomMargin = std::max<int16_t>(6, textHeight / 5);
  int16_t spriteWidth = textWidth + 6;
  int16_t spriteHeight = textHeight + topMargin + bottomMargin;
  int16_t baseline = spriteHeight - bottomMargin;

  if (!wordSprite.createSprite(spriteWidth, spriteHeight)) {
    wordSprite.unloadFont();
    ensureFontLoaded_();
    tft.setTextColor(color_, bgColor_);
    tft.setTextWrap(false);
    int16_t h = tft.fontHeight();
    if (h <= 0) h = textHeight;
    int16_t w = tft.textWidth(word);
    if (w <= 0) w = textWidth;
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;
    tft.setCursor(x, baseline);
    tft.print(word);
    return;
  }

  wordSprite.fillSprite(bgColor_);
  wordSprite.setTextDatum(BL_DATUM);
  wordSprite.drawString(word, 0, baseline);

  int32_t minX = spriteWidth, minY = spriteHeight;
  int32_t maxX = -1, maxY = -1;
  for (int32_t y = 0; y < spriteHeight; ++y) {
    for (int32_t x = 0; x < spriteWidth; ++x) {
      if (wordSprite.readPixel(x, y) != bgColor_) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX < minX || maxY < minY) {
    wordSprite.unloadFont();
    wordSprite.deleteSprite();
    return;
  }

  int32_t contentW = maxX - minX + 1;
  int32_t contentH = maxY - minY + 1;

  const float targetWidth = TFT_W * 0.8f;
  float scale = targetWidth / static_cast<float>(contentW);
  if (scale < 1.0f) scale = 1.0f;

  const float maxScaleHeight = static_cast<float>(TFT_H) / static_cast<float>(contentH);
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
    int32_t srcY = minY + ((dy * contentH) + scaledH / 2) / scaledH;
    if (srcY > maxY) srcY = maxY;
    for (int32_t dx = 0; dx < scaledW; ++dx) {
      int32_t srcX = minX + ((dx * contentW) + scaledW / 2) / scaledW;
      if (srcX > maxX) srcX = maxX;
      uint16_t pixel = wordSprite.readPixel(srcX, srcY);
      if (pixel != bgColor_) {
        int32_t drawX = destX + dx;
        int32_t drawY = destY + dy;
        if (drawX >= 0 && drawX < TFT_W && drawY >= 0 && drawY < TFT_H) {
          tft.drawPixel(drawX, drawY, pixel);
        }
      }
    }
  }

  wordSprite.unloadFont();
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

  chooseFont_();

  TFT_eSprite letterSprite(&tft);
  letterSprite.setColorDepth(16);
  letterSprite.setTextWrap(false);
  letterSprite.setTextColor(color_, bgColor_);

  if (!loadSpriteFont_(letterSprite, fontName_)) {
    ensureFontLoaded_();
    tft.setTextColor(color_, bgColor_);
    tft.setTextWrap(false);
    int16_t h = tft.fontHeight();
    if (h <= 0) h = 32;
    int16_t w = tft.textWidth(letter);
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;
    tft.setCursor(x, baseline);
    tft.print(letter);
    return;
  }

  int16_t textHeight = letterSprite.fontHeight();
  if (textHeight <= 0) textHeight = 32;
  int16_t textWidth = letterSprite.textWidth(letter.c_str());
  if (textWidth <= 0) textWidth = 1;

  int16_t topMargin = std::max<int16_t>(6, textHeight / 5);
  int16_t bottomMargin = std::max<int16_t>(8, textHeight / 4);
  int16_t spriteWidth = textWidth + 8;
  int16_t spriteHeight = textHeight + topMargin + bottomMargin;
  int16_t baseline = spriteHeight - bottomMargin;

  if (!letterSprite.createSprite(spriteWidth, spriteHeight)) {
    letterSprite.unloadFont();
    ensureFontLoaded_();
    tft.setTextColor(color_, bgColor_);
    tft.setTextWrap(false);
    int16_t h = tft.fontHeight();
    if (h <= 0) h = textHeight;
    int16_t w = tft.textWidth(letter);
    int16_t x = (TFT_W - w) / 2;
    if (x < 0) x = 0;
    int16_t baseline = (TFT_H + h) / 2 - 1;
    tft.setCursor(x, baseline);
    tft.print(letter);
    return;
  }

  letterSprite.fillSprite(bgColor_);
  letterSprite.setTextDatum(BL_DATUM);
  letterSprite.drawString(letter, 0, baseline);

  // Compute tight bounding box to keep glyph centred after baseline offset
  int32_t minX = spriteWidth, minY = spriteHeight;
  int32_t maxX = -1, maxY = -1;
  for (int32_t y = 0; y < spriteHeight; ++y) {
    for (int32_t x = 0; x < spriteWidth; ++x) {
      if (letterSprite.readPixel(x, y) != bgColor_) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX < minX || maxY < minY) {
    letterSprite.unloadFont();
    letterSprite.deleteSprite();
    return;
  }

  int32_t contentW = maxX - minX + 1;
  int32_t contentH = maxY - minY + 1;

  const float targetHeight = TFT_H * 0.7f;
  float scale = targetHeight / static_cast<float>(contentH);
  if (scale < 1.0f) scale = 1.0f;

  const float maxScaleWidth = (TFT_W * 0.8f) / static_cast<float>(contentW);
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
    int32_t srcY = minY + ((dy * contentH) + scaledH / 2) / scaledH;
    if (srcY > maxY) srcY = maxY;
    for (int32_t dx = 0; dx < scaledW; ++dx) {
      int32_t srcX = minX + ((dx * contentW) + scaledW / 2) / scaledW;
      if (srcX > maxX) srcX = maxX;
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

  letterSprite.unloadFont();
  letterSprite.deleteSprite();
}

void TextApp::nextMode_() {
  uint8_t current = static_cast<uint8_t>(mode_);
  current = (current + 1) % 3;
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

void TextApp::cycleAlignment_() {
  uint8_t next = (static_cast<uint8_t>(alignment_) + 1) % 3;
  alignment_ = static_cast<TextAlign>(next);
  needsRedraw_ = true;
  showStatus_(String("Text: ") + alignmentName_());
}

void TextApp::reloadConfig_() {
  mode_ = DisplayMode::TextBlock;
  text_ = "Hallo Welt!";
  color_ = 0xFFFF;
  bgColor_ = 0x0000;
  textSize_ = 2;
  fontExplicit_ = false;
  fontLoaded_ = false;
  fontLoadFailed_ = false;
  fontName_ = "FreeSansBold18pt";
  alignment_ = TextAlign::Center;
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
  unloadFont_();
  tft.setTextFont(4);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  tft.setTextColor(TFT_WHITE, bgColor_);

  int16_t h = tft.fontHeight();
  if (h <= 0) h = 32;
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
    case DisplayMode::TextBlock: return "Text";
    case DisplayMode::BigWords: return "Woerter";
    case DisplayMode::BigLetters: return "Buchstaben";
  }
  return "?";
}

const char* TextApp::alignmentName_() const {
  switch (alignment_) {
    case TextAlign::Left: return "links";
    case TextAlign::Center: return "zentriert";
    case TextAlign::Right: return "rechts";
  }
  return "?";
}

void TextApp::chooseFont_() {
  if (!fontExplicit_) {
    if (textSize_ >= 4) {
      fontName_ = "FreeSansBold24pt";
    } else {
      fontName_ = "FreeSansBold18pt";
    }
  }
  if (fontName_.isEmpty()) {
    fontName_ = "FreeSansBold18pt";
  }
}

void TextApp::applyFont_() {
  chooseFont_();
  loadFont_(fontName_);
}

bool TextApp::loadFont_(const String& name) {
  unloadFont_();

  String path = resolveFontPath_(name);
  if (path.isEmpty()) {
    fontLoadFailed_ = true;
    fontLoaded_ = false;
    tft.unloadFont();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    return false;
  }

  String canonical = path;
  if (canonical.endsWith(".vlw")) {
    canonical.remove(canonical.length() - 4);
  }
  while (canonical.startsWith("/")) {
    canonical.remove(0, 1);
  }

  String fullPath = "/" + canonical + ".vlw";
  if (!LittleFS.exists(fullPath)) {
    #ifdef USB_DEBUG
      Serial.printf("[TextApp] Font missing: %s\n", fullPath.c_str());
    #endif
    fontLoadFailed_ = true;
    fontLoaded_ = false;
    tft.unloadFont();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    return false;
  }

  tft.loadFont(canonical, LittleFS);
  tft.setTextWrap(false);
  fontLoaded_ = true;
  fontLoadFailed_ = false;
  return true;
}

void TextApp::unloadFont_() {
  if (fontLoaded_) {
    tft.unloadFont();
  }
  fontLoaded_ = false;
}

bool TextApp::ensureFontLoaded_() {
  if (fontLoaded_) return true;
  if (fontLoadFailed_) {
    tft.setTextWrap(false);
    return false;
  }
  return loadFont_(fontName_);
}

String TextApp::resolveFontPath_(const String& name) const {
  if (name.isEmpty()) return String();

  String cleaned = name;
  cleaned.trim();
  if (cleaned.endsWith(".vlw")) {
    cleaned.remove(cleaned.length() - 4);
  }
  while (cleaned.startsWith("/")) {
    cleaned.remove(0, 1);
  }

  String lower = cleaned;
  lower.toLowerCase();

  const FontMapEntry* entry = lookupFont(cleaned);
  if (entry) {
    return String(entry->path);
  }

  if (cleaned.indexOf('/') < 0) {
    return String("system/fonts/") + lower;
  }

  return lower;
}

bool TextApp::loadSpriteFont_(TFT_eSprite& sprite, const String& name) {
  String path = resolveFontPath_(name);
  if (path.isEmpty()) return false;

  String canonical = path;
  if (canonical.endsWith(".vlw")) {
    canonical.remove(canonical.length() - 4);
  }
  while (canonical.startsWith("/")) {
    canonical.remove(0, 1);
  }

  String fullPath = "/" + canonical + ".vlw";
  if (!LittleFS.exists(fullPath)) {
    return false;
  }

  sprite.unloadFont();
  sprite.loadFont(canonical, LittleFS);
  int16_t fh = sprite.fontHeight();
  if (fh <= 0) {
    sprite.unloadFont();
    return false;
  }
  return true;
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
