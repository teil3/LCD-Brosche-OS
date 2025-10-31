#pragma once
#ifndef PLUGIN_APP_H_
#define PLUGIN_APP_H_

#include "App.h"
#include "AppAPI.h"
#include <Arduino.h>
#include <LittleFS.h>

/**
 * PluginApp - Wrapper for dynamically loaded plugin apps
 *
 * This class implements the App interface and loads plugin .bin files
 * from LittleFS at runtime.
 */
class PluginApp : public App {
public:
  PluginApp(const char* binPath);
  ~PluginApp();

  // App interface
  const char* name() const override;
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

  // Check if plugin loaded successfully
  bool isLoaded() const { return loaded_; }

private:
  String binPath_;
  bool loaded_ = false;
  void* pluginMemory_ = nullptr;
  size_t pluginSize_ = 0;
  PluginAppVTable vtable_;
  String appName_;

  // Core API provided to plugins
  static AppAPI coreAPI_;

  // Load plugin from LittleFS
  bool loadPlugin_();
  void unloadPlugin_();

  // Find VTable in loaded binary
  bool findVTable_();
};

#endif // PLUGIN_APP_H_
