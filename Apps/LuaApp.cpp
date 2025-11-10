#include "LuaApp.h"

#include <algorithm>
#include <memory>

#include "Core/Gfx.h"
#include "Core/TextRenderer.h"
#include "Core/Storage.h"
#include "Config.h"

namespace {
constexpr const char* kScriptsDir = "/scripts";
constexpr const char* kDefaultScript = "/scripts/main.lua";

const char kEmbeddedScript[] = R"LUA(
-- Default Lua demo
local t = 0
function setup()
  brosche.clear()
  brosche.log("Lua demo gestartet")
end

function loop(dt)
  t = (t + dt) % 2000
  local r = (t % 255)
  local g = ((t * 2) % 255)
  local b = ((t * 3) % 255)
  brosche.fill(brosche.rgb(r, g, b))
  brosche.text(120, 120, string.format("%d", t), brosche.rgb(0, 0, 0))
end
)LUA";

uint16_t clamp8Lua(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint16_t>(value);
}
}

void LuaApp::init() {
  ensureScriptsDir_();
  scanScripts_();
  scriptIndex_ = 0;
  lastScanMs_ = millis();
  createVm_();
  tft.fillScreen(TFT_BLACK);
  TextRenderer::drawCentered(120, "Lua initialisieren", TFT_WHITE, TFT_BLACK);

  bool ok = false;
  if (!scripts_.empty()) {
    ok = loadCurrentScript_();
  }
  if (!ok && LittleFS.exists(kDefaultScript)) {
    ok = loadScript_(kDefaultScript) && runSetup_();
  }
  if (!ok) {
    ok = runDefaultScript_() && runSetup_();
  }

  if (!ok) {
    lastError_ = lastError_.isEmpty() ? String("Keine Skripte") : lastError_;
    drawStatus_();
  }
}

void LuaApp::tick(uint32_t delta_ms) {
  if (!vmReady_ || !scriptLoaded_) return;

  if (millis() - lastScanMs_ >= kScriptScanIntervalMs) {
    lastScanMs_ = millis();
    scanScripts_();
  }

  if (!pushFunction_("loop")) {
    return;
  }
  lua_pushinteger(L_, static_cast<lua_Integer>(delta_ms));
  if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
    handleLuaError_();
  }
}

void LuaApp::draw() {
  if (!vmReady_) {
    drawStatus_();
  }
}

void LuaApp::shutdown() {
  destroyVm_();
}

void LuaApp::onButton(uint8_t index, BtnEvent e) {
  if (!vmReady_) return;

  if (index == 2) {
    if (e == BtnEvent::Single) {
      nextScript_();
      return;
    } else if (e == BtnEvent::Long) {
      prevScript_();
      return;
    }
  }

  if (!scriptLoaded_) return;

  const char* eventName = nullptr;
  switch (e) {
    case BtnEvent::Single: eventName = "single"; break;
    case BtnEvent::Double: eventName = "double"; break;
    case BtnEvent::Triple: eventName = "triple"; break;
    case BtnEvent::Long:   eventName = "long"; break;
    default: return;
  }

  if (!pushFunction_("onButton")) {
    return;
  }
  lua_pushinteger(L_, index);
  lua_pushstring(L_, eventName);
  if (lua_pcall(L_, 2, 0, 0) != LUA_OK) {
    handleLuaError_();
  }
}

void LuaApp::createVm_() {
  destroyVm_();
  L_ = luaL_newstate();
  if (!L_) {
    lastError_ = "Lua: kein Speicher";
    vmReady_ = false;
    return;
  }

  lua_atpanic(L_, [](lua_State* L) -> int {
    const char* msg = lua_tostring(L, -1);
    Serial.printf("[Lua] panic: %s\n", msg ? msg : "<nil>");
    return 0;
  });

  luaL_requiref(L_, LUA_GNAME, luaopen_base, 1); lua_pop(L_, 1);
  luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L_, 1);
  luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L_, 1);
  luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L_, 1);
  luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L_, 1);

  lua_newtable(L_);
  lua_pushcfunction(L_, lua_fill); lua_setfield(L_, -2, "fill");
  lua_pushcfunction(L_, lua_clear); lua_setfield(L_, -2, "clear");
  lua_pushcfunction(L_, lua_rect); lua_setfield(L_, -2, "rect");
  lua_pushcfunction(L_, lua_text); lua_setfield(L_, -2, "text");
  lua_pushcfunction(L_, lua_rgb); lua_setfield(L_, -2, "rgb");
  lua_pushcfunction(L_, lua_print); lua_setfield(L_, -2, "log");
  lua_setglobal(L_, "brosche");

  vmReady_ = true;
}

void LuaApp::destroyVm_() {
  if (L_) {
    lua_close(L_);
    L_ = nullptr;
  }
  vmReady_ = false;
  scriptLoaded_ = false;
}

bool LuaApp::loadScript_(const char* path) {
  if (!vmReady_) return false;

  File f = LittleFS.open(path, FILE_READ);
  if (!f) {
    lastError_ = String("Kann ") + path + " nicht öffnen";
    return false;
  }

  size_t size = f.size();
  if (size == 0) {
    lastError_ = "Lua-Skript leer";
    f.close();
    return false;
  }

  std::unique_ptr<char[]> buffer(new (std::nothrow) char[size]);
  if (!buffer) {
    lastError_ = "Lua: kein Speicher";
    f.close();
    return false;
  }
  size_t read = f.readBytes(buffer.get(), size);
  f.close();
  if (read != size) {
    lastError_ = "Lua: Lese-Fehler";
    return false;
  }

  if (luaL_loadbuffer(L_, buffer.get(), size, path) != LUA_OK) {
    handleLuaError_();
    return false;
  }
  if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
    handleLuaError_();
    return false;
  }
  scriptPath_ = path;
  scriptLoaded_ = true;
  return true;
}

bool LuaApp::runDefaultScript_() {
  if (!vmReady_) return false;
  if (luaL_loadbuffer(L_, kEmbeddedScript, sizeof(kEmbeddedScript) - 1, "default.lua") != LUA_OK) {
    handleLuaError_();
    return false;
  }
  if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
    handleLuaError_();
    return false;
  }
  scriptPath_ = "<embedded>";
  scriptLoaded_ = true;
  return true;
}

bool LuaApp::pushFunction_(const char* name) {
  lua_getglobal(L_, name);
  if (!lua_isfunction(L_, -1)) {
    lua_pop(L_, 1);
    return false;
  }
  return true;
}

bool LuaApp::runSetup_() {
  if (!scriptLoaded_) return false;
  if (!pushFunction_("setup")) {
    return true;
  }
  if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
    handleLuaError_();
    return false;
  }
  return true;
}

bool LuaApp::loadCurrentScript_() {
  if (scripts_.empty()) {
    lastError_ = "Keine Skripte";
    drawStatus_();
    return false;
  }
  if (!loadScript_(scripts_[scriptIndex_].c_str())) {
    drawStatus_();
    return false;
  }
  if (!runSetup_()) {
    return false;
  }
  showScriptInfo_();
  return true;
}

void LuaApp::nextScript_() {
  if (scripts_.empty()) {
    lastError_ = "Keine Skripte";
    drawStatus_();
    return;
  }
  scriptIndex_ = (scriptIndex_ + 1) % scripts_.size();
  loadCurrentScript_();
}

void LuaApp::prevScript_() {
  if (scripts_.empty()) {
    lastError_ = "Keine Skripte";
    drawStatus_();
    return;
  }
  scriptIndex_ = (scriptIndex_ == 0) ? scripts_.size() - 1 : scriptIndex_ - 1;
  loadCurrentScript_();
}

void LuaApp::handleLuaError_() {
  const char* msg = lua_tostring(L_, -1);
  lastError_ = msg ? msg : String("Lua Fehler");
  lua_pop(L_, 1);
  Serial.printf("[Lua] error: %s\n", lastError_.c_str());
  vmReady_ = false;
  scriptLoaded_ = false;
}

void LuaApp::drawStatus_() {
  tft.fillScreen(TFT_BLACK);
  TextRenderer::drawCentered(120, lastError_.isEmpty() ? String("Lua Status") : lastError_, TFT_WHITE, TFT_BLACK);
}

void LuaApp::ensureScriptsDir_() {
  if (!LittleFS.exists(kScriptsDir)) {
    LittleFS.mkdir(kScriptsDir);
  }
}

void LuaApp::scanScripts_() {
  std::vector<String> found;
  File dir = LittleFS.open(kScriptsDir);
  if (!dir || !dir.isDirectory()) {
    lastError_ = "scripts-Verz. fehlt";
    return;
  }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    String name = f.name();
    if (!name.endsWith(".lua")) continue;
    found.push_back(name);
  }
  dir.close();
  std::sort(found.begin(), found.end());
  if (found.empty()) {
    scripts_.clear();
    scriptIndex_ = 0;
    return;
  }
  String current = scripts_.empty() ? String() : scripts_[scriptIndex_];
  scripts_ = std::move(found);
  if (!current.isEmpty()) {
    auto it = std::find(scripts_.begin(), scripts_.end(), current);
    scriptIndex_ = (it == scripts_.end()) ? 0 : static_cast<size_t>(std::distance(scripts_.begin(), it));
  } else {
    scriptIndex_ = 0;
  }
}

void LuaApp::showScriptInfo_() {
  if (scripts_.empty()) return;
  String display = scripts_[scriptIndex_];
  int slash = display.lastIndexOf('/');
  if (slash >= 0) display = display.substring(slash + 1);
  tft.fillRect(0, 0, TFT_W, TextRenderer::lineHeight() + 6, TFT_BLACK);
  TextRenderer::drawCentered(2, String("Lua: ") + display, TFT_WHITE, TFT_BLACK);
}

int LuaApp::lua_fill(lua_State* L) {
  uint32_t color = luaL_checkinteger(L, 1);
  tft.fillScreen(color);
  return 0;
}

int LuaApp::lua_clear(lua_State* L) {
  uint32_t color = luaL_optinteger(L, 1, TFT_BLACK);
  tft.fillScreen(color);
  return 0;
}

int LuaApp::lua_rect(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int w = luaL_checkinteger(L, 3);
  int h = luaL_checkinteger(L, 4);
  uint32_t color = luaL_optinteger(L, 5, TFT_WHITE);
  tft.fillRect(x, y, w, h, color);
  return 0;
}

int LuaApp::lua_text(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  const char* text = luaL_checkstring(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);
  uint32_t bg = luaL_optinteger(L, 5, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(text, x, y);
  return 0;
}

int LuaApp::lua_rgb(lua_State* L) {
  int r = clamp8Lua(luaL_checkinteger(L, 1));
  int g = clamp8Lua(luaL_checkinteger(L, 2));
  int b = clamp8Lua(luaL_checkinteger(L, 3));
  lua_pushinteger(L, tft.color565(r, g, b));
  return 1;
}

int LuaApp::lua_print(lua_State* L) {
  int nargs = lua_gettop(L);
  for (int i = 1; i <= nargs; ++i) {
    const char* txt = lua_tostring(L, i);
    Serial.print(txt ? txt : "nil");
    if (i < nargs) Serial.print("\t");
  }
  Serial.println();
  return 0;
}
