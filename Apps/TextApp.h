#pragma once
#include "Core/App.h"
#include "Core/I18n.h"
#include <Arduino.h>
#include <vector>

class TFT_eSPI;
class TFT_eSprite;

class TextApp : public App {
public:
  const char* name() const override { return i18n.t("apps.text"); }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

private:
  enum class DisplayMode : uint8_t {
    TextBlock = 0,
    BigWords = 1,
    BigLetters = 2
  };

  enum class TextAlign : uint8_t {
    Left = 0,
    Center = 1,
    Right = 2
  };

  // Configuration
  DisplayMode mode_ = DisplayMode::TextBlock;
  String text_ = "Hallo Welt!";
  uint16_t color_ = 0xFFFF;  // White
  uint16_t bgColor_ = 0x0000;  // Black
  uint8_t textSize_ = 2;
  String fontName_ = "FreeSansBold18pt";
  bool fontLoaded_ = false;
  bool fontLoadFailed_ = false;
  bool fontExplicit_ = false;
  TextAlign alignment_ = TextAlign::Center;
  uint32_t letterSpeed_ = 1000;  // Big display change speed (ms per step)

  // Runtime state
  uint32_t timeAccum_ = 0;
  size_t bigLetterIndex_ = 0;
  size_t bigWordIndex_ = 0;
  bool wordsDirty_ = true;
  std::vector<String> words_;
  std::vector<String> pages_;
  size_t currentPageIndex_ = 0;
  bool pagesDirty_ = true;
  uint32_t pauseUntil_ = 0;
  bool needsRedraw_ = true;
  bool needsFullRedraw_ = true;
  uint8_t speedIndex_ = 2;  // Index into speed table
  // Methods
  void loadConfig_();
  void parseConfigLine_(const String& line);
  DisplayMode stringToMode_(const String& str) const;
  uint16_t parseColor_(const String& str) const;
  void drawTextBlock_();
  void drawBigLetter_();
  void drawBigWord_();
  void nextMode_();
  void cycleSpeed_();
  void reloadConfig_();
  void cycleAlignment_();
  void showStatus_(const String& msg);
  uint32_t currentSpeed_() const;
  const char* modeName_() const;
  const char* alignmentName_() const;
  void chooseFont_();
  void applyFont_();
  bool loadFont_(const String& name);
  void unloadFont_();
  bool ensureFontLoaded_();
  String resolveFontPath_(const String& name) const;
  bool loadSpriteFont_(TFT_eSprite& sprite, const String& name);
  void rebuildWords_();
  void rebuildPages_();
};
