#pragma once
#include "Core/App.h"
#include <Arduino.h>

class TextApp : public App {
public:
  const char* name() const override { return "Text"; }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

private:
  enum class DisplayMode : uint8_t {
    SingleLine = 0,
    MultiLine = 1,
    Marquee = 2,
    BigLetters = 3
  };

  // Configuration
  DisplayMode mode_ = DisplayMode::SingleLine;
  String text_ = "Hallo Welt!";
  uint16_t color_ = 0xFFFF;  // White
  uint16_t bgColor_ = 0x0000;  // Black
  uint8_t textSize_ = 2;
  uint32_t marqueeSpeed_ = 50;  // Marquee scroll speed (ms per pixel)
  uint32_t letterSpeed_ = 1000;  // Big letter change speed (ms per letter)

  // Runtime state
  uint32_t timeAccum_ = 0;
  int16_t marqueeX_ = 0;
  int16_t marqueeLastX_ = 0;
  int16_t marqueeWidth_ = 0;
  int16_t marqueeY_ = 0;
  size_t bigLetterIndex_ = 0;
  uint32_t pauseUntil_ = 0;
  bool needsRedraw_ = true;
  bool needsFullRedraw_ = true;
  uint8_t speedIndex_ = 2;  // Index into speed table

  // Methods
  void loadConfig_();
  void parseConfigLine_(const String& line);
  DisplayMode stringToMode_(const String& str) const;
  uint16_t parseColor_(const String& str) const;
  void drawSingleLine_();
  void drawMultiLine_();
  void drawMarquee_();
  void drawBigLetter_();
  void nextMode_();
  void cycleSpeed_();
  void reloadConfig_();
  void showStatus_(const String& msg);
  uint32_t currentSpeed_() const;
  const char* modeName_() const;
};
