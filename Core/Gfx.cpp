#include "Gfx.h"
#include "TextRenderer.h"

TFT_eSPI tft;
SPIClass sdSPI(VSPI);

static bool tft_output_cb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (x >= tft.width() || y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

void gfxBegin() {
  // CS-Leitungen sicher HIGH
  pinMode(TFT_CS_PIN, OUTPUT); digitalWrite(TFT_CS_PIN, HIGH);
  pinMode(SD_CS_PIN,  OUTPUT); digitalWrite(SD_CS_PIN,  HIGH);

  // --- SD zuerst, konservativ wie im funktionierenden Sketch ---
  sdSPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);

  // während SD-Init: Display sicher deselektiert lassen
  digitalWrite(TFT_CS_PIN, HIGH);
  bool sd_ok = SD.begin(SD_CS_PIN, sdSPI, 5000000);     // 5 MHz
  if (!sd_ok) {
    #ifdef USB_DEBUG
      Serial.println("[GFX] SD @5MHz FAIL, retry @2MHz");
    #endif
    sd_ok = SD.begin(SD_CS_PIN, sdSPI, 2000000);        // Fallback 2 MHz
  }
  if (!sd_ok) {
    #ifdef USB_DEBUG
      Serial.println("[GFX] SD init FAILED");
    #endif
  } else {
    #ifdef USB_DEBUG
      // Mini-Diagnose
      uint8_t type = SD.cardType();
      Serial.printf("[GFX] SD OK, type=%u\n", type);
      File root = SD.open("/");
      if (root) {
        int shown = 0;
        for (File f = root.openNextFile(); f && shown < 5; f = root.openNextFile(), ++shown) {
          Serial.printf("[GFX] /%s%s\n", f.isDirectory()?"<DIR> ":"", f.name());
        }
      }
    #endif
  }

  // --- TFT danach ---
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  TextRenderer::begin();

  // JPEG-Decoder (nach TFT, aber unabhängig vom SD-Init)
  TJpgDec.setCallback(tft_output_cb);
  TJpgDec.setSwapBytes(true);

  #ifdef USB_DEBUG
    Serial.println("[GFX] init ok");
  #endif
}
