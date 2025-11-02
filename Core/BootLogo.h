#ifndef CORE_BOOTLOGO_H
#define CORE_BOOTLOGO_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>
#include "Config.h"

// ─────────────────────────────────────────────────────────────────────────────
// Bootlogo zeichnen (zentriert) und optional für `ms` anzeigen
// Loads bootlogo from LittleFS at /system/bootlogo.jpg
inline void drawBootLogo(TFT_eSPI& tft, uint32_t ms) {
  if (ms == 0) {
    return;
  }
  tft.fillScreen(TFT_BLACK);

  // Try to load from LittleFS
  if (LittleFS.exists("/system/bootlogo.jpg")) {
    File f = LittleFS.open("/system/bootlogo.jpg", FILE_READ);
    if (f) {
      size_t fileSize = f.size();
      if (fileSize > 0 && fileSize < 50000) {  // Sanity check
        uint8_t* buffer = (uint8_t*)malloc(fileSize);
        if (buffer) {
          size_t read = f.readBytes((char*)buffer, fileSize);
          if (read == fileSize) {
            // Get JPEG dimensions to center it
            uint16_t jpegWidth = 0, jpegHeight = 0;
            TJpgDec.getJpgSize(&jpegWidth, &jpegHeight, buffer, fileSize);

            // Calculate centered position
            int x = (TFT_W - jpegWidth) / 2;
            int y = (TFT_H - jpegHeight) / 2;

            tft.startWrite();
            TJpgDec.drawJpg(x, y, buffer, fileSize);
            tft.endWrite();
          }
          free(buffer);
        }
      }
      f.close();
    }
  }

  const uint32_t t0 = millis();
  while (millis() - t0 < ms) { delay(10); yield(); }
}

#endif // CORE_BOOTLOGO_H
