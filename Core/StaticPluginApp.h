#pragma once
#ifndef STATIC_PLUGIN_APP_H_
#define STATIC_PLUGIN_APP_H_

#include "App.h"
#include "AppAPI.h"
#include "AppAPIImpl.h"

/**
 * StaticPluginApp - Wrapper for statically linked plugins
 *
 * This is a simplified version of PluginApp for PoC testing.
 * Instead of loading from .bin files, it uses a statically
 * linked VTable. This proves the concept before we tackle
 * the complex dynamic loading.
 */
class StaticPluginApp : public App {
public:
  StaticPluginApp(const PluginAppVTable* vtable);

  // App interface
  const char* name() const override;
  void init() override;
  void tick(uint32_t delta_ms) override;
  void onButton(uint8_t index, BtnEvent e) override;
  void draw() override;
  void shutdown() override;

private:
  const PluginAppVTable* vtable_;
  String appName_;

  // Reference to shared core API
  static AppAPI& coreAPI_;
};

#endif // STATIC_PLUGIN_APP_H_
