#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"
#include "Core/Gfx.h"
#include "Core/BootLogo.h"
#include "Core/Buttons.h"
#include "Core/AppManager.h"
#include "Apps/SlideshowApp.h"
#include "Apps/RandomSmallPixelIntoneApp.h"
#include "Apps/RandomPixelIntoneApp.h"
#include "Apps/RandomChaoticLinesApp.h"
#include "Apps/RandomStripesIntoneApp.h"
#include "Core/Storage.h"

// Buttons
ButtonState btn1({(uint8_t)BTN1_PIN, true});
ButtonState btn2({(uint8_t)BTN2_PIN, true});

static const char* btnEventName(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single: return "Single";
    case BtnEvent::Double: return "Double";
    case BtnEvent::Triple: return "Triple";
    case BtnEvent::Long:   return "Long";
    default:               return "None";
  }
}

// Apps
AppManager appman;
SlideshowApp app_slideshow;
RandomSmallPixelIntoneApp app_pixel_field;
RandomPixelIntoneApp app_pixel_blocks;
RandomChaoticLinesApp app_random_lines;
RandomStripesIntoneApp app_random_stripes;

void setup() {
  Serial.begin(115200);
  delay(100);
  // Warten, bis der Monitor dran ist (nur kurz)
  for (int i=0;i<20;i++){ if (Serial) break; delay(10); }
  Serial.println("[BOOT] setup start");

  bool lfs_ok = LittleFS.begin();
  if (!lfs_ok) {
    Serial.println("[BOOT] LittleFS mount failed, try format");
    lfs_ok = LittleFS.begin(true);
  }
  if (lfs_ok) {
    if (!ensureFlashSlidesDir()) {
      Serial.println("[BOOT] ensureFlashSlidesDir failed");
    } else {
      Serial.println("[BOOT] LittleFS ready");
    }
  } else {
    Serial.println("[BOOT] LittleFS unavailable");
  }

  gfxBegin();
  Serial.println("[BOOT] gfxBegin done");

  drawBootLogo(tft, BOOT_MS);
  Serial.println("[BOOT] bootlogo done");

  btn1.begin(); btn2.begin();
  Serial.println("[BOOT] buttons ready");

  appman.add(&app_slideshow);
  appman.add(&app_pixel_field);
  appman.add(&app_pixel_blocks);
  appman.add(&app_random_lines);
  appman.add(&app_random_stripes);
  appman.begin();
  Serial.println("[BOOT] appman.begin done");
}


void loop() {
  static bool first=true;
  if (first) { Serial.println("[LOOP] enter"); first=false; }
  static uint32_t last = millis();
  uint32_t now = millis();
  uint32_t dt = now - last;
  last = now;

  // Globale Button-Logik (BTN1): App-Wechsel & Backlight
  BtnEvent e1 = btn1.poll();
  if (e1 != BtnEvent::None) {
    Serial.printf("[BTN] BTN1 %s\n", btnEventName(e1));
    switch (e1) {
      case BtnEvent::Single: appman.next(); break;
      case BtnEvent::Double: appman.prev(); break;
      case BtnEvent::Triple: /* frei: z.B. App-List OSD */ break;
      case BtnEvent::Long:   setBacklight(!backlightOn()); break;
      default: break;
    }
  }

  // App-seitige Events (BTN2)
  BtnEvent e2 = btn2.poll();
  if (e2 != BtnEvent::None) {
    Serial.printf("[BTN] BTN2 %s\n", btnEventName(e2));
    appman.dispatchBtn(2, e2);
  }

  appman.tick(dt);
  appman.draw();

  delay(5);
}

// --- force-compile local cpp units (Arduino ignores subfolders otherwise)
#include "Core/Buttons.cpp"
#include "Core/AppManager.cpp"
#include "Core/Gfx.cpp"
#include "Core/Storage.cpp"
#include "Core/TinyFont.cpp"
#include "Apps/SlideshowApp.cpp"
#include "Apps/RandomSmallPixelIntoneApp.cpp"
#include "Apps/RandomPixelIntoneApp.cpp"
#include "Apps/RandomChaoticLinesApp.cpp"
#include "Apps/RandomStripesIntoneApp.cpp"
