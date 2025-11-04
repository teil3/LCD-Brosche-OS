// Minimal GFX font header tailored for TextApp bitmap font set.
#ifndef _GFXFONT_H_
#define _GFXFONT_H_

#ifdef LOAD_GFXFF

typedef struct {
  uint32_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
} GFXglyph;

typedef struct {
  uint8_t* bitmap;
  GFXglyph* glyph;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
} GFXfont;

#include "FreeSans18pt7b.h"
#include "FreeSansBold18pt7b.h"
#include "FreeSansBold24pt7b.h"
#include "FreeSerif18pt7b.h"

#endif // LOAD_GFXFF

#endif // _GFXFONT_H_
