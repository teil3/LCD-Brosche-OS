#ifndef CORE_GFX_H
#define CORE_GFX_H

#ifndef SMOOTH_FONT
#define SMOOTH_FONT
#endif

// LOAD_GFXFF removed - no longer needed as we use VLW fonts only
// Built-in fonts (1-8) work without LOAD_GFXFF

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "Config.h"

extern TFT_eSPI tft;
extern SPIClass sdSPI;

void gfxBegin();

#endif // CORE_GFX_H
