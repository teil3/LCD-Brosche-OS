#pragma once

#include "Core/App.h"
#include "Core/I18n.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <vector>

extern TFT_eSPI tft;

extern "C" {
#include "Core/Lua/lua.h"
#include "Core/Lua/lauxlib.h"
#include "Core/Lua/lualib.h"
}

class LuaApp : public App {
public:
  const char* name() const override { return i18n.t("apps.lua"); }
  void init() override;
  void tick(uint32_t delta_ms) override;
  void draw() override;
  void shutdown() override;
  void onButton(uint8_t index, BtnEvent e) override;
  bool handleSystemNextRequest();

private:
  bool customFontActive_ = false;
  String currentFontPath_;
  lua_State* L_ = nullptr;
  bool vmReady_ = false;
  bool scriptLoaded_ = false;
  String scriptPath_;
  String scriptDisplayName_;
  String lastError_;
  std::vector<String> scripts_;
  size_t scriptIndex_ = 0;
  uint32_t lastScanMs_ = 0;
  static constexpr uint32_t kScriptScanIntervalMs = 2000;
  uint32_t lastLoopMs_ = 0;
  bool inErrorOverlay_ = false;

  void createVm_();
  void destroyVm_();
  bool loadScript_(const char* path);
  bool runDefaultScript_();
  bool pushFunction_(const char* name);
  bool runSetup_();
  bool loadCurrentScript_();
  void nextScript_();
  void prevScript_();
  bool hasNextScript_() const;
  bool loadTftFont_(const String& path);
  void restoreDefaultFont_();
  String sanitizeFontPath_(const char* raw) const;
  void handleLuaError_();
  void drawStatus_();
  void ensureScriptsDir_();
  void scanScripts_();
  void showScriptInfo_();
  String determineScriptDisplayName_(const String& path);
  String fallbackNameForPath_(const String& path) const;

  // Lua bindings
  static int lua_fill(lua_State* L);
  static int lua_rect(lua_State* L);
  static int lua_line(lua_State* L);
  static int lua_circle(lua_State* L);
  static int lua_fillCircle(lua_State* L);
  static int lua_triangle(lua_State* L);
  static int lua_fillTriangle(lua_State* L);
  static int lua_text(lua_State* L);
  static int lua_rgb(lua_State* L);
  static int lua_print(lua_State* L);
  static int lua_clear(lua_State* L);
  static int lua_loadFont(lua_State* L);
  static int lua_unloadFont(lua_State* L);
  static int lua_temperature(lua_State* L);
  static int lua_millis(lua_State* L);
  static int lua_fs_read(lua_State* L);
  static int lua_fs_write(lua_State* L);
  static int lua_fs_list(lua_State* L);
};
