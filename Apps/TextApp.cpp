#include "TextApp.h"
#include "Config.h"
#include "Core/Gfx.h"
#include "Core/Storage.h"
#include "Core/TextRenderer.h"
#include <LittleFS.h>

// TextApp uses bitmap fonts instead of smooth fonts for scalability

namespace {
constexpr const char* kConfigPath = "/textapp.cfg";
constexpr uint32_t kMarqueeSpeeds[] = {10, 30, 50, 100, 200};
constexpr uint8_t kMarqueeSpeedCount = sizeof(kMarqueeSpeeds) / sizeof(kMarqueeSpeeds[0]);
constexpr uint32_t kLetterSpeeds[] = {200, 500, 1000, 2000, 3000};
constexpr uint8_t kLetterSpeedCount = sizeof(kLetterSpeeds) / sizeof(kLetterSpeeds[0]);
}

void TextApp::init() {
  #ifdef USB_DEBUG
    Serial.println("[TextApp] init() starting");
  #endif

  timeAccum_ = 0;
  marqueeX_ = TFT_W;
  marqueeLastX_ = TFT_W;
  marqueeWidth_ = 0;
  marqueeY_ = 0;
  bigLetterIndex_ = 0;
  pauseUntil_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;
  speedIndex_ = 2;  // Default to 50ms

  // Properly unload smooth font and use scalable bitmap font
  TextRenderer::end();  // This sets fontLoaded=false and unloads font
  tft.setTextFont(1);   // Use built-in font 1 (standard font, 8px base, scalable)
  tft.setTextSize(1);   // Reset to size 1 first

  #ifdef USB_DEBUG
    Serial.println("[TextApp] Switched to bitmap font");
  #endif

  tft.fillScreen(bgColor_);
  loadConfig_();

  #ifdef USB_DEBUG
    Serial.println("[TextApp] init() complete");
  #endif
}

void TextApp::tick(uint32_t delta_ms) {
  if (pauseUntil_) {
    uint32_t now = millis();
    if (now < pauseUntil_) {
      return;
    }
    pauseUntil_ = 0;
    needsRedraw_ = true;
    tft.fillScreen(bgColor_);
  }

  if (mode_ == DisplayMode::Marquee) {
    timeAccum_ += delta_ms;
    while (timeAccum_ >= marqueeSpeed_) {
      timeAccum_ -= marqueeSpeed_;
      marqueeX_--;
      if (marqueeX_ < -marqueeWidth_) {
        marqueeX_ = TFT_W;
      }
      needsRedraw_ = true;
    }
  } else if (mode_ == DisplayMode::BigLetters) {
    timeAccum_ += delta_ms;
    while (timeAccum_ >= letterSpeed_) {
      timeAccum_ -= letterSpeed_;
      bigLetterIndex_++;
      if (bigLetterIndex_ >= text_.length()) {
        bigLetterIndex_ = 0;
      }
      needsRedraw_ = true;
    }
  }
}

void TextApp::draw() {
  if (!needsRedraw_) {
    return;
  }
  needsRedraw_ = false;

  switch (mode_) {
    case DisplayMode::SingleLine:
      drawSingleLine_();
      break;
    case DisplayMode::MultiLine:
      drawMultiLine_();
      break;
    case DisplayMode::Marquee:
      drawMarquee_();
      break;
    case DisplayMode::BigLetters:
      drawBigLetter_();
      break;
  }
}

void TextApp::shutdown() {
  // Restore smooth font for other apps
  #ifdef USB_DEBUG
    Serial.println("[TextApp] Restoring smooth font...");
  #endif

  TextRenderer::begin();  // Reload smooth font (will unload bitmap font first)

  #ifdef USB_DEBUG
    Serial.println("[TextApp] Smooth font restored");
  #endif
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
  #ifdef USB_DEBUG
    Serial.println("[TextApp] loadConfig_() called");
  #endif

  // LittleFS is already mounted in setup(), no need to mount again

  if (!LittleFS.exists(kConfigPath)) {
    #ifdef USB_DEBUG
      Serial.printf("[TextApp] Config not found at %s, using defaults\n", kConfigPath);
    #endif
    return;
  }

  File f = LittleFS.open(kConfigPath, "r");
  if (!f) {
    #ifdef USB_DEBUG
      Serial.println("[TextApp] Failed to open config");
    #endif
    return;
  }

  #ifdef USB_DEBUG
    Serial.printf("[TextApp] Config file opened, size: %lu bytes\n", static_cast<unsigned long>(f.size()));
  #endif

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0 && !line.startsWith("#")) {
      #ifdef USB_DEBUG
        Serial.printf("[TextApp] Parsing line: %s\n", line.c_str());
      #endif
      parseConfigLine_(line);
    }
  }
  f.close();

  #ifdef USB_DEBUG
    Serial.printf("[TextApp] Config loaded: mode=%d, text='%s', color=0x%04X, bg=0x%04X, size=%d, marquee_speed=%lu, letter_speed=%lu\n",
                  static_cast<int>(mode_), text_.c_str(), color_, bgColor_, textSize_,
                  static_cast<unsigned long>(marqueeSpeed_), static_cast<unsigned long>(letterSpeed_));
  #endif
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
    // Replace | with newline for multi-line mode
    text_.replace("|", "\n");
  } else if (key.equalsIgnoreCase("COLOR")) {
    color_ = parseColor_(value);
  } else if (key.equalsIgnoreCase("BG_COLOR") || key.equalsIgnoreCase("BGCOLOR")) {
    bgColor_ = parseColor_(value);
  } else if (key.equalsIgnoreCase("SIZE")) {
    textSize_ = value.toInt();
    if (textSize_ < 1) textSize_ = 1;
    if (textSize_ > 10) textSize_ = 10;
  } else if (key.equalsIgnoreCase("MARQUEE_SPEED")) {
    marqueeSpeed_ = value.toInt();
    if (marqueeSpeed_ < 10) marqueeSpeed_ = 10;
    if (marqueeSpeed_ > 5000) marqueeSpeed_ = 5000;
  } else if (key.equalsIgnoreCase("LETTER_SPEED")) {
    letterSpeed_ = value.toInt();
    if (letterSpeed_ < 10) letterSpeed_ = 10;
    if (letterSpeed_ > 10000) letterSpeed_ = 10000;
  } else if (key.equalsIgnoreCase("SPEED")) {
    // Legacy support: set both speeds
    uint32_t spd = value.toInt();
    if (spd >= 10 && spd <= 5000) {
      marqueeSpeed_ = spd;
      letterSpeed_ = spd * 10;  // Letters are slower
    }
  }
}

TextApp::DisplayMode TextApp::stringToMode_(const String& str) const {
  if (str.equalsIgnoreCase("single_line")) return DisplayMode::SingleLine;
  if (str.equalsIgnoreCase("multi_line")) return DisplayMode::MultiLine;
  if (str.equalsIgnoreCase("marquee")) return DisplayMode::Marquee;
  if (str.equalsIgnoreCase("big_letters")) return DisplayMode::BigLetters;
  return DisplayMode::SingleLine;
}

uint16_t TextApp::parseColor_(const String& str) const {
  // Parse hex color (e.g., "0xFFFF" or "FFFF")
  String hex = str;
  if (hex.startsWith("0x") || hex.startsWith("0X")) {
    hex = hex.substring(2);
  }

  unsigned long val = strtoul(hex.c_str(), nullptr, 16);
  return static_cast<uint16_t>(val & 0xFFFF);
}

void TextApp::drawSingleLine_() {
  if (needsFullRedraw_) {
    tft.fillScreen(bgColor_);
    needsFullRedraw_ = false;
  }

  tft.setTextSize(textSize_);
  tft.setTextColor(color_, bgColor_);  // Set background to avoid artifacts

  // Calculate centered position
  int16_t w = tft.textWidth(text_.c_str());
  int16_t h = tft.fontHeight();

  int16_t x = (TFT_W - w) / 2;
  int16_t y = (TFT_H - h) / 2;

  tft.setCursor(x, y);
  tft.print(text_);
}

void TextApp::drawMultiLine_() {
  if (needsFullRedraw_) {
    tft.fillScreen(bgColor_);
    needsFullRedraw_ = false;
  }

  tft.setTextSize(textSize_);
  tft.setTextColor(color_, bgColor_);  // Set background to avoid artifacts

  // Split text by newlines and draw each line centered
  String remaining = text_;
  int16_t startY = 20;  // Start from top with margin
  int16_t lineHeight = tft.fontHeight() + 4;  // Line height with spacing

  while (remaining.length() > 0) {
    int nlPos = remaining.indexOf('\n');
    String line;
    if (nlPos >= 0) {
      line = remaining.substring(0, nlPos);
      remaining = remaining.substring(nlPos + 1);
    } else {
      line = remaining;
      remaining = "";
    }

    if (line.length() > 0) {
      int16_t w = tft.textWidth(line.c_str());

      int16_t x = (TFT_W - w) / 2;
      tft.setCursor(x, startY);
      tft.print(line);

      startY += lineHeight;
      if (startY >= TFT_H) break;  // Stop if we run out of screen space
    }
  }
}

void TextApp::drawMarquee_() {
  tft.setTextSize(textSize_);

  // Prepare text for marquee: replace newlines with spaces
  String displayText = text_;
  displayText.replace("\n", " ");

  // Calculate text dimensions if not done yet
  if (marqueeWidth_ == 0) {
    marqueeWidth_ = tft.textWidth(displayText.c_str());
    marqueeY_ = (TFT_H - tft.fontHeight()) / 2;
  }

  // Full redraw on first frame or after mode change
  if (needsFullRedraw_) {
    tft.fillScreen(bgColor_);
    needsFullRedraw_ = false;
    marqueeLastX_ = marqueeX_;
  } else {
    // Smart clear: only clear the regions that need it
    int16_t textHeight = tft.fontHeight();

    // If text moved left, clear the trailing edge
    if (marqueeX_ < marqueeLastX_) {
      int16_t clearX = marqueeX_ + marqueeWidth_;
      int16_t clearW = marqueeLastX_ - marqueeX_;

      // Clamp to screen bounds
      if (clearX < TFT_W && clearW > 0) {
        if (clearX + clearW > TFT_W) {
          clearW = TFT_W - clearX;
        }
        tft.fillRect(clearX, marqueeY_, clearW, textHeight, bgColor_);
      }

      // Also clear the wrapped text on the right if text went off screen
      if (marqueeLastX_ + marqueeWidth_ > TFT_W && marqueeX_ < 0) {
        int16_t wrapClearX = 0;
        int16_t wrapClearW = -marqueeX_;
        if (wrapClearW > marqueeWidth_) wrapClearW = marqueeWidth_;
        tft.fillRect(wrapClearX, marqueeY_, wrapClearW, textHeight, bgColor_);
      }
    }
  }

  // Draw text at current position with background color for clean overdraw
  tft.setTextColor(color_, bgColor_);
  tft.setCursor(marqueeX_, marqueeY_);
  tft.print(displayText);

  marqueeLastX_ = marqueeX_;
}

void TextApp::drawBigLetter_() {
  if (needsFullRedraw_) {
    tft.fillScreen(bgColor_);
    needsFullRedraw_ = false;
  } else {
    // Clear screen for big letters (they're large and change completely)
    tft.fillScreen(bgColor_);
  }

  if (text_.length() == 0 || bigLetterIndex_ >= text_.length()) {
    return;
  }

  char letter[2] = {text_[bigLetterIndex_], '\0'};

  // Use large text size to fill screen
  uint8_t bigSize = 10;  // Maximum size
  tft.setTextSize(bigSize);
  tft.setTextColor(color_);

  // Calculate centered position
  int16_t w = tft.textWidth(letter);
  int16_t h = tft.fontHeight();

  int16_t x = (TFT_W - w) / 2;
  int16_t y = (TFT_H - h) / 2;

  tft.setCursor(x, y);
  tft.print(letter);
}

void TextApp::nextMode_() {
  uint8_t current = static_cast<uint8_t>(mode_);
  current = (current + 1) % 4;
  mode_ = static_cast<DisplayMode>(current);

  // Reset animation state
  marqueeX_ = TFT_W;
  marqueeLastX_ = TFT_W;
  marqueeWidth_ = 0;  // Force recalculation with new mode
  marqueeY_ = 0;
  bigLetterIndex_ = 0;
  timeAccum_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;

  tft.fillScreen(bgColor_);
  showStatus_(String("Modus: ") + modeName_());

  #ifdef USB_DEBUG
    Serial.printf("[TextApp] Mode changed to %s\n", modeName_());
  #endif
}

void TextApp::cycleSpeed_() {
  // Cycle speed based on current mode
  if (mode_ == DisplayMode::Marquee) {
    speedIndex_ = (speedIndex_ + 1) % kMarqueeSpeedCount;
    marqueeSpeed_ = kMarqueeSpeeds[speedIndex_];
    timeAccum_ = 0;
    showStatus_(String("Tempo: ") + String(marqueeSpeed_) + "ms");
    #ifdef USB_DEBUG
      Serial.printf("[TextApp] Marquee speed changed to %lums\n", static_cast<unsigned long>(marqueeSpeed_));
    #endif
  } else if (mode_ == DisplayMode::BigLetters) {
    speedIndex_ = (speedIndex_ + 1) % kLetterSpeedCount;
    letterSpeed_ = kLetterSpeeds[speedIndex_];
    timeAccum_ = 0;
    showStatus_(String("Tempo: ") + String(letterSpeed_) + "ms");
    #ifdef USB_DEBUG
      Serial.printf("[TextApp] Letter speed changed to %lums\n", static_cast<unsigned long>(letterSpeed_));
    #endif
  }
}

void TextApp::reloadConfig_() {
  #ifdef USB_DEBUG
    Serial.println("[TextApp] reloadConfig_() called");
  #endif

  // Reset to defaults
  mode_ = DisplayMode::SingleLine;
  text_ = "Hallo Welt!";
  color_ = 0xFFFF;
  bgColor_ = 0x0000;
  textSize_ = 2;
  marqueeSpeed_ = 50;
  letterSpeed_ = 1000;

  loadConfig_();

  // Reset animation state
  marqueeX_ = TFT_W;
  marqueeLastX_ = TFT_W;
  marqueeWidth_ = 0;
  marqueeY_ = 0;
  bigLetterIndex_ = 0;
  timeAccum_ = 0;
  needsRedraw_ = true;
  needsFullRedraw_ = true;

  // Clear screen and show status
  tft.fillScreen(bgColor_);
  showStatus_("Config geladen");

  #ifdef USB_DEBUG
    Serial.println("[TextApp] Config reloaded successfully");
  #endif
}

void TextApp::showStatus_(const String& msg) {
  // Draw status message using current bitmap font (don't trigger TextRenderer!)
  tft.setTextSize(2);  // Medium size for status messages
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int16_t w = tft.textWidth(msg.c_str());
  int16_t h = tft.fontHeight();

  int16_t x = (TFT_W - w) / 2;
  int16_t y = (TFT_H - h) / 2;

  // Draw with simple outline for readability
  tft.setTextColor(TFT_BLACK);
  tft.setCursor(x - 1, y);
  tft.print(msg);
  tft.setCursor(x + 1, y);
  tft.print(msg);
  tft.setCursor(x, y - 1);
  tft.print(msg);
  tft.setCursor(x, y + 1);
  tft.print(msg);

  // Draw main text
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(x, y);
  tft.print(msg);

  pauseUntil_ = millis() + 1000;

  #ifdef USB_DEBUG
    Serial.printf("[TextApp] Status shown: %s\n", msg.c_str());
  #endif
}

uint32_t TextApp::currentSpeed_() const {
  // This is now only used for compatibility, actual speeds are mode-specific
  if (mode_ == DisplayMode::Marquee) {
    return marqueeSpeed_;
  } else if (mode_ == DisplayMode::BigLetters) {
    return letterSpeed_;
  }
  return 50;  // Default
}

const char* TextApp::modeName_() const {
  switch (mode_) {
    case DisplayMode::SingleLine: return "1 Zeile";
    case DisplayMode::MultiLine: return "Mehrere";
    case DisplayMode::Marquee: return "Laufschrift";
    case DisplayMode::BigLetters: return "Grosse";
    default: return "?";
  }
}
