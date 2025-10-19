#ifndef CORE_TINYFONT_H
#define CORE_TINYFONT_H

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace TinyFont {
inline constexpr uint8_t kGlyphW = 5;
inline constexpr uint8_t kGlyphH = 7;

uint16_t measure(const String& text, uint8_t scale = 1);
uint16_t glyphHeight(uint8_t scale = 1);
void drawString(TFT_eSPI& tft, int16_t x, int16_t y, const String& text,
                uint16_t fg, uint8_t scale = 1);
void drawStringCentered(TFT_eSPI& tft, int16_t y, const String& text,
                        uint16_t fg, uint8_t scale = 1);
void drawStringOutline(TFT_eSPI& tft, int16_t x, int16_t y, const String& text,
                       uint16_t fg, uint16_t outline, uint8_t scale = 1);
void drawStringOutlineCentered(TFT_eSPI& tft, int16_t y, const String& text,
                               uint16_t fg, uint16_t outline,
                               uint8_t scale = 1);
}

#endif // CORE_TINYFONT_H
