#pragma once
#ifndef APPAPI_H_
#define APPAPI_H_

#include <Arduino.h>
#include <TFT_eSPI.h>

/**
 * AppAPI - Function Table for Plugin Apps
 *
 * This struct provides a stable ABI for dynamically loaded apps.
 * Plugin apps receive a pointer to this struct at initialization.
 */
struct AppAPI {
  // Version info for compatibility checking
  uint32_t version;

  // Display access
  TFT_eSPI* tft;

  // TextRenderer functions
  void (*drawCentered)(int16_t y, const char* text, uint32_t fg, uint32_t bg);
  int16_t (*lineHeight)();

  // Logging
  void (*logInfo)(const char* msg);
  void (*logError)(const char* msg);

  // Timing
  uint32_t (*millis)();
  void (*delay)(uint32_t ms);
};

/**
 * Plugin App Interface
 *
 * Every plugin must implement these functions with C linkage.
 */
extern "C" {
  typedef struct {
    // Called once when app is loaded, receives core API
    void (*init)(const AppAPI* api);

    // Called when app becomes active
    void (*onActivate)();

    // Called every frame (delta_ms = time since last tick)
    void (*tick)(uint32_t delta_ms);

    // Called to render current frame
    void (*draw)();

    // Called on button events (index=1 for BTN1, 2 for BTN2)
    // event: 0=Single, 1=Double, 2=Triple, 3=Long
    void (*onButton)(uint8_t index, uint8_t event);

    // Called when app is deactivated
    void (*shutdown)();

    // Returns app name (must be static string)
    const char* (*getName)();
  } PluginAppVTable;
}

// Current API version (increment on breaking changes)
#define APP_API_VERSION 1

#endif // APPAPI_H_
