#ifndef CORE_BOOTLOGO_H
#define CORE_BOOTLOGO_H
#include <TFT_eSPI.h>
#include "Config.h"
#include "TinyFont.h"

inline void drawBootLogo(TFT_eSPI& tft, uint32_t ms) {
  tft.fillScreen(TFT_BLACK);
  uint8_t scale = 2;
  int16_t y = (TFT_H - TinyFont::glyphHeight(scale)) / 2;
  TinyFont::drawStringOutlineCentered(tft, y, "Boot...", TFT_WHITE, TFT_BLACK,
                                      scale);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) delay(10);
}

#endif // CORE_BOOTLOGO_H
