#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"
#include "Core/Gfx.h"
#include "Core/BootLogo.h"
#include "Core/Buttons.h"
#include "Core/AppManager.h"
#include "Apps/SlideshowApp.h"
#include "Apps/RandomImagerApp.h"
#include "Apps/RandomSquareIntoneApp.h"
#include "Apps/RandomPixelIntoneApp.h"
#include "Apps/RandomChaoticLinesApp.h"
#include "Apps/RandomStripesIntoneApp.h"
#include "Apps/TextApp.h"
#include "Apps/LuaApp.h"
#include "Core/Storage.h"
#include "Core/BleImageTransfer.h"
#include "Core/SerialImageTransfer.h"
#include "Core/SystemUI.h"

// Buttons
ButtonState btn1({(uint8_t)BTN1_PIN, true});
ButtonState btn2({(uint8_t)BTN2_PIN, true});

#ifdef USB_DEBUG
static const char* btnEventName(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single: return "Single";
    case BtnEvent::Double: return "Double";
    case BtnEvent::Triple: return "Triple";
    case BtnEvent::Long:   return "Long";
    default:               return "None";
  }
}
#endif

// Apps
AppManager appman;
SlideshowApp app_slideshow;
RandomImagerApp app_random_imager;
RandomPixelIntoneApp app_pixel_blocks;
RandomSquareIntoneApp app_square_intone;
RandomChaoticLinesApp app_random_lines;
RandomStripesIntoneApp app_random_stripes;
TextApp app_text;
LuaApp app_lua;
SystemUI systemUi;

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
  Serial.setTimeout(5);
  delay(100);
  // Warten, bis der Monitor dran ist (nur kurz)
  for (int i=0;i<20;i++){ if (Serial) break; delay(10); }
  #ifdef USB_DEBUG
    Serial.println("[BOOT] setup start");
  #endif

  bool lfs_ok = mountLittleFs(false);
  if (!lfs_ok) {
    #ifdef USB_DEBUG
      Serial.println("[BOOT] LittleFS mount failed, try format");
    #endif
    lfs_ok = mountLittleFs(true);
  }
  if (lfs_ok) {
    if (!ensureFlashSlidesDir()) {
      #ifdef USB_DEBUG
        Serial.println("[BOOT] ensureFlashSlidesDir failed");
      #endif
    } else {
      #ifdef USB_DEBUG
        Serial.println("[BOOT] LittleFS ready");
      #endif
    }
  } else {
    #ifdef USB_DEBUG
      Serial.println("[BOOT] LittleFS unavailable");
    #endif
  }

  gfxBegin();
  #ifdef USB_DEBUG
    Serial.println("[BOOT] gfxBegin done");
    Serial.printf("[BOOT] BOOT_MS=%u\n", BOOT_MS);
  #endif
  drawBootLogo(tft, BOOT_MS);
  #ifdef USB_DEBUG
    Serial.println("[BOOT] bootlogo done");
  #endif

  btn1.begin(); btn2.begin();
  #ifdef USB_DEBUG
    Serial.println("[BOOT] buttons ready");
  #endif

  appman.add(&app_slideshow);
  appman.add(&app_text);
  appman.add(&app_pixel_blocks);
  appman.add(&app_random_lines);
  appman.add(&app_random_stripes);
  appman.add(&app_random_imager);
  appman.add(&app_square_intone);
  appman.add(&app_lua);
  appman.begin();
  #ifdef USB_DEBUG
    Serial.println("[BOOT] appman.begin done");
  #endif

  SystemUI::Callbacks sysCallbacks;
  sysCallbacks.ensureSlideshowActive = []() { return ensureSlideshowActive(); };
  sysCallbacks.setSource = [](SlideSource src) { return app_slideshow.setSlideSource(src, false, false); };
  sysCallbacks.currentSource = []() { return app_slideshow.slideSource(); };
  sysCallbacks.sourceLabel = []() { return app_slideshow.sourceLabel(); };
  sysCallbacks.focusTransferredFile = [](const char* filename, size_t size) {
    return app_slideshow.focusTransferredFile(filename ? filename : "", size);
  };
  systemUi.begin(sysCallbacks);

  BleImageTransfer::begin();
  #ifdef USB_DEBUG
    Serial.println("[BOOT] BLE ready");
  #endif
  SerialImageTransfer::begin();
  #ifdef USB_DEBUG
    Serial.println("[BOOT] USB ready");
  #endif
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

    switch (evt.type) {
      case BleImageTransfer::EventType::Started:
        systemUi.onTransferStarted(SystemUI::TransferSource::Ble, evt.filename, evt.size);
        break;
      case BleImageTransfer::EventType::Completed:
        systemUi.onTransferCompleted(SystemUI::TransferSource::Ble, evt.filename, evt.size);
        break;
      case BleImageTransfer::EventType::Error:
        systemUi.onTransferError(SystemUI::TransferSource::Ble, evt.message);
        break;
      case BleImageTransfer::EventType::Aborted:
        systemUi.onTransferAborted(SystemUI::TransferSource::Ble, evt.message);
        break;
    }
  }
}

static void pumpUsbEvents() {
  SerialImageTransfer::Event evt;
  while (SerialImageTransfer::pollEvent(&evt)) {
    #ifdef USB_DEBUG
      const char* typeStr = "";
      switch (evt.type) {
        case SerialImageTransfer::EventType::Started:   typeStr = "STARTED"; break;
        case SerialImageTransfer::EventType::Completed: typeStr = "DONE"; break;
        case SerialImageTransfer::EventType::Error:     typeStr = "ERROR"; break;
        case SerialImageTransfer::EventType::Aborted:   typeStr = "ABORT"; break;
      }
      Serial.printf("[USB] EVENT %s size=%lu file=%s msg=%s\n",
                    typeStr,
                    static_cast<unsigned long>(evt.size),
                    evt.filename,
                    evt.message);
    #endif

    switch (evt.type) {
      case SerialImageTransfer::EventType::Started:
        systemUi.onTransferStarted(SystemUI::TransferSource::Usb, evt.filename, evt.size);
        break;
      case SerialImageTransfer::EventType::Completed:
        systemUi.onTransferCompleted(SystemUI::TransferSource::Usb, evt.filename, evt.size);
        break;
      case SerialImageTransfer::EventType::Error:
        systemUi.onTransferError(SystemUI::TransferSource::Usb, evt.message);
        break;
      case SerialImageTransfer::EventType::Aborted:
        systemUi.onTransferAborted(SystemUI::TransferSource::Usb, evt.message);
        break;
    }
  }
}

static bool ensureSlideshowActive() {
  App* active = appman.activeApp();
  if (active == &app_slideshow) {
    return true;
  }
  return appman.activate(&app_slideshow);
}

void loop() {
  #ifdef USB_DEBUG
    static bool first=true;
    if (first) { Serial.println("[LOOP] enter"); first=false; }
  #endif
  static uint32_t last = millis();
  uint32_t now = millis();
  uint32_t dt = now - last;
  last = now;

  // Globale Button-Logik (BTN1): App-Wechsel & Backlight
  BtnEvent e1 = btn1.poll();
  if (e1 != BtnEvent::None) {
    #ifdef USB_DEBUG
      Serial.printf("[BTN] BTN1 %s\n", btnEventName(e1));
    #endif
    bool handled = systemUi.isActive() && systemUi.handleButton(1, e1);
    if (!handled) {
      switch (e1) {
        case BtnEvent::Single: {
          App* active = appman.activeApp();
          if (active == &app_lua) {
            if (!app_lua.handleSystemNextRequest()) {
              appman.next();
            }
          } else {
            appman.next();
          }
          break;
        }
        case BtnEvent::Double:
          systemUi.showSetup();
          break;
        case BtnEvent::Triple:
          break;
        case BtnEvent::Long:
          appman.prev();
          break;
        default:
          break;
      }
    }
  }

  // App-seitige Events (BTN2)
  BtnEvent e2 = btn2.poll();
  if (e2 != BtnEvent::None) {
    #ifdef USB_DEBUG
      Serial.printf("[BTN] BTN2 %s\n", btnEventName(e2));
    #endif
    if (!systemUi.handleButton(2, e2)) {
      appman.dispatchBtn(2, e2);
    }
  }

  bool overlayActive = systemUi.shouldPauseApps();
  App* currentApp = appman.activeApp();
  bool slideshowActive = (currentApp == &app_slideshow);
  if (slideshowActive) {
    app_slideshow.setUiLocked(overlayActive);
  } else if (!overlayActive && app_slideshow.isUiLocked()) {
    app_slideshow.setUiLocked(false);
  }

  SerialImageTransfer::tick();
  pumpUsbEvents();

  if (!overlayActive) {
    appman.tick(dt);
    appman.draw();
  }

  SerialImageTransfer::tick();
  pumpUsbEvents();
  BleImageTransfer::tick();
  pumpBleEvents();

  systemUi.draw();

  delay(1);
}

// --- force-compile local cpp units (Arduino ignores subfolders otherwise)
#include "Core/Buttons.cpp"
#include "Core/AppManager.cpp"
#include "Core/SetupMenu.cpp"
#include "Core/SystemUI.cpp"
#include "Core/Gfx.cpp"
#include "Core/Storage.cpp"
#include "Core/TextRenderer.cpp"
#include "Apps/SlideshowApp.cpp"
#include "Apps/RandomImagerApp.cpp"
#include "Apps/RandomSquareIntoneApp.cpp"
#include "Apps/RandomPixelIntoneApp.cpp"
#include "Apps/RandomChaoticLinesApp.cpp"
#include "Apps/RandomStripesIntoneApp.cpp"
#include "Apps/TextApp.cpp"
#include "Core/BleImageTransfer.cpp"
#include "Core/SerialImageTransfer.cpp"
#include "Apps/LuaApp.cpp"
#include "Core/Lua/lua_runtime.cpp"
