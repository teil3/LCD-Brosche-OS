#ifndef CORE_GFX_H
#define CORE_GFX_H

#ifndef SMOOTH_FONT
#define SMOOTH_FONT
#endif

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "Config.h"

extern TFT_eSPI tft;
extern SPIClass sdSPI;

void gfxBegin();
void setBacklight(bool on);
bool backlightOn();

#endif // CORE_GFX_H
