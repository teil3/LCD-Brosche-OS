#ifndef CORE_BOOTLOGO_H
#define CORE_BOOTLOGO_H
#include <TFT_eSPI.h>
#include "Config.h"

inline void drawBootLogo(TFT_eSPI& tft, uint32_t ms) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Boot...", TFT_W/2, TFT_H/2, 2);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) delay(10);
}

#endif // CORE_BOOTLOGO_H

