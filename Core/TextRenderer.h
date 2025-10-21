#pragma once

#include <Arduino.h>

namespace TextRenderer {

void begin();
void end();

int16_t lineHeight();
int16_t ascent();
int16_t descent();
int16_t measure(const String& text);

void draw(int16_t x, int16_t yTop, const String& text, uint16_t fgColor, uint16_t outlineColor);
void drawCentered(int16_t yTop, const String& text, uint16_t fgColor, uint16_t outlineColor);

}  // namespace TextRenderer

