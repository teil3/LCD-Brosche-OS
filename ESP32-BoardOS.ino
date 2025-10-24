#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"
#include "Core/Gfx.h"
#include "Core/BootLogo.h"
#include "Core/Buttons.h"
#include "Core/AppManager.h"
#include "Apps/SlideshowApp.h"
#include "Apps/PixelFieldApp.h"
#include "Apps/RandomImagerApp.h"
#include "Apps/RandomPastellerApp.h"
#include "Apps/RandomSquareIntoneApp.h"
#include "Apps/RandomPixelIntoneApp.h"
#include "Apps/RandomChaoticLinesApp.h"
#include "Apps/RandomStripesIntoneApp.h"
#include "Core/Storage.h"
#include "Core/BleImageTransfer.h"

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
PixelFieldApp app_pixel_field;
RandomImagerApp app_random_imager;
RandomPastellerApp app_random_pasteller;
RandomPixelIntoneApp app_pixel_blocks;
RandomSquareIntoneApp app_square_intone;
RandomChaoticLinesApp app_random_lines;
RandomStripesIntoneApp app_random_stripes;

void setup() {
  Serial.begin(115200);
  delay(100);
  // Warten, bis der Monitor dran ist (nur kurz)
  for (int i=0;i<20;i++){ if (Serial) break; delay(10); }
  Serial.println("[BOOT] setup start");

  bool lfs_ok = mountLittleFs(false);
  if (!lfs_ok) {
    Serial.println("[BOOT] LittleFS mount failed, try format");
    lfs_ok = mountLittleFs(true);
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

  Serial.printf("[BOOT] BOOT_MS=%u\n", BOOT_MS);
  drawBootLogo(tft, BOOT_MS);
  Serial.println("[BOOT] bootlogo done");

  btn1.begin(); btn2.begin();
  Serial.println("[BOOT] buttons ready");

  appman.add(&app_slideshow);
  appman.add(&app_pixel_field);
  appman.add(&app_pixel_blocks);
  appman.add(&app_random_lines);
  appman.add(&app_random_stripes);
  appman.add(&app_random_imager);
  appman.add(&app_random_pasteller);
  appman.add(&app_square_intone);
  appman.begin();
  Serial.println("[BOOT] appman.begin done");

  BleImageTransfer::begin();
  Serial.println("[BOOT] BLE ready");
}

static void pumpBleEvents() {
  BleImageTransfer::Event evt;
  while (BleImageTransfer::pollEvent(&evt)) {
    const char* typeStr = "";
    switch (evt.type) {
      case BleImageTransfer::EventType::Started:   typeStr = "STARTED"; break;
      case BleImageTransfer::EventType::Completed: typeStr = "DONE"; break;
      case BleImageTransfer::EventType::Error:     typeStr = "ERROR"; break;
      case BleImageTransfer::EventType::Aborted:   typeStr = "ABORT"; break;
    }
    Serial.printf("[BLE] EVENT %s size=%lu file=%s msg=%s\n",
                  typeStr,
                  static_cast<unsigned long>(evt.size),
                  evt.filename,
                  evt.message);

    App* active = appman.activeApp();
    if (active == &app_slideshow) {
      switch (evt.type) {
        case BleImageTransfer::EventType::Started:
          app_slideshow.onBleTransferStarted(evt.filename, evt.size);
          break;
        case BleImageTransfer::EventType::Completed:
          app_slideshow.onBleTransferCompleted(evt.filename, evt.size);
          break;
        case BleImageTransfer::EventType::Error:
          app_slideshow.onBleTransferError(evt.message);
          break;
        case BleImageTransfer::EventType::Aborted:
          app_slideshow.onBleTransferAborted(evt.message);
          break;
      }
    }
  }
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
      case BtnEvent::Double: /* frei: z.B. App-List OSD */ break;
      case BtnEvent::Triple: /* frei */ break;
      case BtnEvent::Long:   appman.prev(); break;
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

  BleImageTransfer::tick();
  pumpBleEvents();

  delay(5);
}

// --- force-compile local cpp units (Arduino ignores subfolders otherwise)
#include "Core/Buttons.cpp"
#include "Core/AppManager.cpp"
#include "Core/Gfx.cpp"
#include "Core/Storage.cpp"
#include "Core/TextRenderer.cpp"
#include "Apps/SlideshowApp.cpp"
#include "Apps/PixelFieldApp.cpp"
#include "Apps/RandomImagerApp.cpp"
#include "Apps/RandomPastellerApp.cpp"
#include "Apps/RandomSquareIntoneApp.cpp"
#include "Apps/RandomPixelIntoneApp.cpp"
#include "Apps/RandomChaoticLinesApp.cpp"
#include "Apps/RandomStripesIntoneApp.cpp"
#include "Core/BleImageTransfer.cpp"
