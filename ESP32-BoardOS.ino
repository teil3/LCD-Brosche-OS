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
#include "Core/SerialImageTransfer.h"
#include "Core/StaticPluginApp.h"
#include "Core/PluginApp.h"
#include <SD.h>

// Buttons
ButtonState btn1({(uint8_t)BTN1_PIN, true});
ButtonState btn2({(uint8_t)BTN2_PIN, true});

namespace {
constexpr bool kUsbDebug = false;
}

static const char* btnEventName(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single: return "Single";
    case BtnEvent::Double: return "Double";
    case BtnEvent::Triple: return "Triple";
    case BtnEvent::Long:   return "Long";
    default:               return "None";
  }
}

// Forward declaration for HelloWorld plugin
extern "C" const PluginAppVTable* getHelloWorldVTable();

// Dynamic plugins storage (max 10 dynamic plugins)
constexpr int MAX_DYNAMIC_PLUGINS = 10;
PluginApp* dynamicPlugins[MAX_DYNAMIC_PLUGINS] = {nullptr};
int dynamicPluginCount = 0;

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
StaticPluginApp app_helloworld(getHelloWorldVTable());

/**
 * Discover and load dynamic plugins from LittleFS and SD card
 * Searches /apps/*.bin in both filesystems
 */
void discoverPlugins() {
  Serial.println("[Plugins] === Plugin Discovery Start ===");

  auto scanDirectory = [](fs::FS &fs, const char* fsName) {
    const char* dirPath = "/apps";

    if (!fs.exists(dirPath)) {
      Serial.printf("[Plugins] %s:%s not found, skipping\n", fsName, dirPath);
      return;
    }

    File root = fs.open(dirPath);
    if (!root || !root.isDirectory()) {
      Serial.printf("[Plugins] %s:%s is not a directory\n", fsName, dirPath);
      return;
    }

    Serial.printf("[Plugins] Scanning %s:%s\n", fsName, dirPath);

    File file = root.openNextFile();
    while (file && dynamicPluginCount < MAX_DYNAMIC_PLUGINS) {
      String filename = String(file.name());

      // Extract just the filename if it contains path
      int lastSlash = filename.lastIndexOf('/');
      if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
      }

      if (filename.endsWith(".bin")) {
        String fullPath = String(dirPath) + "/" + filename;
        Serial.printf("[Plugins] Found: %s:%s\n", fsName, fullPath.c_str());

        PluginApp* plugin = new PluginApp(fullPath.c_str());
        if (plugin && plugin->isLoaded()) {
          dynamicPlugins[dynamicPluginCount++] = plugin;
          appman.add(plugin);
          Serial.printf("[Plugins] ✓ Loaded: %s\n", plugin->name());
        } else {
          Serial.printf("[Plugins] ✗ Failed to load: %s\n", fullPath.c_str());
          if (plugin) delete plugin;
        }
      }

      file = root.openNextFile();
    }

    root.close();
  };

  // Scan LittleFS first
  scanDirectory(LittleFS, "LittleFS");

  // Then scan SD card
  scanDirectory(SD, "SD");

  Serial.printf("[Plugins] === Discovery Complete: %d plugins loaded ===\n", dynamicPluginCount);
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
  Serial.setTimeout(5);
  delay(100);
  // Warten, bis der Monitor dran ist (nur kurz)
  for (int i=0;i<20;i++){ if (Serial) break; delay(10); }
  if (kUsbDebug) Serial.println("[BOOT] setup start");

  bool lfs_ok = mountLittleFs(false);
  if (!lfs_ok) {
    if (kUsbDebug) Serial.println("[BOOT] LittleFS mount failed, try format");
    lfs_ok = mountLittleFs(true);
  }
  if (lfs_ok) {
    if (!ensureFlashSlidesDir()) {
      if (kUsbDebug) Serial.println("[BOOT] ensureFlashSlidesDir failed");
    } else {
      if (kUsbDebug) Serial.println("[BOOT] LittleFS ready");
    }
  } else {
    if (kUsbDebug) Serial.println("[BOOT] LittleFS unavailable");
  }

  gfxBegin();
  if (kUsbDebug) Serial.println("[BOOT] gfxBegin done");

  if (kUsbDebug) Serial.printf("[BOOT] BOOT_MS=%u\n", BOOT_MS);
  drawBootLogo(tft, BOOT_MS);
  if (kUsbDebug) Serial.println("[BOOT] bootlogo done");

  btn1.begin(); btn2.begin();
  if (kUsbDebug) Serial.println("[BOOT] buttons ready");

  appman.add(&app_slideshow);
  appman.add(&app_helloworld);
  appman.add(&app_pixel_field);
  appman.add(&app_pixel_blocks);
  appman.add(&app_random_lines);
  appman.add(&app_random_stripes);
  appman.add(&app_random_imager);
  appman.add(&app_random_pasteller);
  appman.add(&app_square_intone);

  appman.begin();

  // NOTE: Plugin discovery moved to loop() to avoid boot watchdog timeout
  if (kUsbDebug) Serial.println("[BOOT] appman.begin done");

  BleImageTransfer::begin();
  if (kUsbDebug) Serial.println("[BOOT] BLE ready");
  SerialImageTransfer::begin();
  if (kUsbDebug) Serial.println("[BOOT] USB ready");
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

static void pumpUsbEvents() {
  SerialImageTransfer::Event evt;
  while (SerialImageTransfer::pollEvent(&evt)) {
    const char* typeStr = "";
    switch (evt.type) {
      case SerialImageTransfer::EventType::Started:   typeStr = "STARTED"; break;
      case SerialImageTransfer::EventType::Completed: typeStr = "DONE"; break;
      case SerialImageTransfer::EventType::Error:     typeStr = "ERROR"; break;
      case SerialImageTransfer::EventType::Aborted:   typeStr = "ABORT"; break;
    }
    if (kUsbDebug) {
      Serial.printf("[USB] EVENT %s size=%lu file=%s msg=%s\n",
                    typeStr,
                    static_cast<unsigned long>(evt.size),
                    evt.filename,
                    evt.message);
    }

    App* active = appman.activeApp();
    if (active == &app_slideshow) {
      switch (evt.type) {
        case SerialImageTransfer::EventType::Started:
          app_slideshow.onUsbTransferStarted(evt.filename, evt.size);
          break;
        case SerialImageTransfer::EventType::Completed:
          app_slideshow.onUsbTransferCompleted(evt.filename, evt.size);
          break;
        case SerialImageTransfer::EventType::Error:
          app_slideshow.onUsbTransferError(evt.message);
          break;
        case SerialImageTransfer::EventType::Aborted:
          app_slideshow.onUsbTransferAborted(evt.message);
          break;
      }
    }
  }
}

void loop() {
  static bool first=true;
  if (first) {
    Serial.println("[LOOP] enter");

    // Discover and load dynamic plugins AFTER setup() to avoid boot watchdog
    Serial.println("[LOOP] Starting plugin discovery...");
    discoverPlugins();
    Serial.println("[LOOP] Plugin discovery done");

    first=false;
  }

  static uint32_t last = millis();
  uint32_t now = millis();
  uint32_t dt = now - last;
  last = now;

  // Globale Button-Logik (BTN1): App-Wechsel & Backlight
  BtnEvent e1 = btn1.poll();
  if (e1 != BtnEvent::None) {
    if (kUsbDebug) Serial.printf("[BTN] BTN1 %s\n", btnEventName(e1));
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
    if (kUsbDebug) Serial.printf("[BTN] BTN2 %s\n", btnEventName(e2));
    appman.dispatchBtn(2, e2);
  }

  SerialImageTransfer::tick();
  pumpUsbEvents();

  appman.tick(dt);
  appman.draw();

  SerialImageTransfer::tick();
  pumpUsbEvents();
  BleImageTransfer::tick();
  pumpBleEvents();

  delay(1);
}

// --- force-compile local cpp units (Arduino ignores subfolders otherwise)
#include "Core/Buttons.cpp"
#include "Core/AppManager.cpp"
#include "Core/Gfx.cpp"
#include "Core/Storage.cpp"
#include "Core/TextRenderer.cpp"
#include "Core/AppAPIImpl.cpp"
#include "Core/StaticPluginApp.cpp"
#include "Core/PluginApp.cpp"
#include "Apps/SlideshowApp.cpp"
#include "Apps/HelloWorldApp.cpp"
#include "Apps/PixelFieldApp.cpp"
#include "Apps/RandomImagerApp.cpp"
#include "Apps/RandomPastellerApp.cpp"
#include "Apps/RandomSquareIntoneApp.cpp"
#include "Apps/RandomPixelIntoneApp.cpp"
#include "Apps/RandomChaoticLinesApp.cpp"
#include "Apps/RandomStripesIntoneApp.cpp"
#include "Core/BleImageTransfer.cpp"
#include "Core/SerialImageTransfer.cpp"
