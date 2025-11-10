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
APP_NAME = "Lua Demo"
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

LuaApp* gActiveLuaApp = nullptr;
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
    if (ok) {
      showScriptInfo_();
    }
  }
  if (!ok) {
    ok = runDefaultScript_() && runSetup_();
    if (ok) {
      showScriptInfo_();
    }
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
  restoreDefaultFont_();
  destroyVm_();
}

void LuaApp::onButton(uint8_t index, BtnEvent e) {
  if (!vmReady_) return;

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

  gActiveLuaApp = this;

  lua_newtable(L_);
  lua_pushcfunction(L_, lua_fill); lua_setfield(L_, -2, "fill");
  lua_pushcfunction(L_, lua_clear); lua_setfield(L_, -2, "clear");
  lua_pushcfunction(L_, lua_rect); lua_setfield(L_, -2, "rect");
  lua_pushcfunction(L_, lua_line); lua_setfield(L_, -2, "line");
  lua_pushcfunction(L_, lua_circle); lua_setfield(L_, -2, "circle");
  lua_pushcfunction(L_, lua_fillCircle); lua_setfield(L_, -2, "fillCircle");
  lua_pushcfunction(L_, lua_triangle); lua_setfield(L_, -2, "triangle");
  lua_pushcfunction(L_, lua_fillTriangle); lua_setfield(L_, -2, "fillTriangle");
  lua_pushcfunction(L_, lua_text); lua_setfield(L_, -2, "text");
  lua_pushcfunction(L_, lua_rgb); lua_setfield(L_, -2, "rgb");
  lua_pushcfunction(L_, lua_print); lua_setfield(L_, -2, "log");
  lua_pushcfunction(L_, lua_loadFont); lua_setfield(L_, -2, "loadFont");
  lua_pushcfunction(L_, lua_unloadFont); lua_setfield(L_, -2, "unloadFont");
  lua_pushcfunction(L_, lua_temperature); lua_setfield(L_, -2, "temperature");
  lua_pushcfunction(L_, lua_millis); lua_setfield(L_, -2, "time");
  lua_pushcfunction(L_, lua_fs_read); lua_setfield(L_, -2, "readFile");
  lua_pushcfunction(L_, lua_fs_write); lua_setfield(L_, -2, "writeFile");
  lua_pushcfunction(L_, lua_fs_list); lua_setfield(L_, -2, "listFiles");
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
  if (gActiveLuaApp == this) {
    gActiveLuaApp = nullptr;
  }
  restoreDefaultFont_();
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
  scriptDisplayName_ = determineScriptDisplayName_(String(path));
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
  scriptDisplayName_ = determineScriptDisplayName_(String(scriptPath_));
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

bool LuaApp::hasNextScript_() const {
  if (scripts_.empty()) {
    return false;
  }
  return (scriptIndex_ + 1) < scripts_.size();
}

bool LuaApp::handleSystemNextRequest() {
  if (!vmReady_) {
    return false;
  }
  if (!hasNextScript_()) {
    return false;
  }
  ++scriptIndex_;
  return loadCurrentScript_();
}

void LuaApp::handleLuaError_() {
  const char* msg = lua_tostring(L_, -1);
  lastError_ = msg ? msg : String("Lua Fehler");
  lua_pop(L_, 1);
  Serial.printf("[Lua] error: %s\n", lastError_.c_str());
  vmReady_ = false;
  scriptLoaded_ = false;
}

bool LuaApp::loadTftFont_(const String& path) {
  if (path.isEmpty()) {
    lastError_ = "Font-Pfad leer";
    return false;
  }

  String canonical = path;
  if (canonical.endsWith(".vlw")) {
    canonical.remove(canonical.length() - 4);
  }
  while (canonical.startsWith("/")) {
    canonical.remove(0, 1);
  }

  String chosen = canonical;
  String fullPath = "/" + chosen + ".vlw";
  if (!LittleFS.exists(fullPath)) {
    String lower = canonical;
    lower.toLowerCase();
    String lowerPath = "/" + lower + ".vlw";
    if (LittleFS.exists(lowerPath)) {
      chosen = lower;
      fullPath = lowerPath;
    } else {
      lastError_ = String("Font fehlt: ") + fullPath;
      return false;
    }
  }

  File font = LittleFS.open(fullPath, FILE_READ);
  size_t size = font.size();
  font.close();
  if (size == 0) {
    lastError_ = "Font-Datei leer";
    return false;
  }

  bool defaultActive = !customFontActive_;
  if (defaultActive) {
    TextRenderer::end();
  } else {
    tft.unloadFont();
  }

  tft.loadFont(chosen.c_str(), LittleFS);
  customFontActive_ = true;
  currentFontPath_ = fullPath;
  return true;
}

void LuaApp::restoreDefaultFont_() {
  if (!customFontActive_) {
    return;
  }
  tft.unloadFont();
  TextRenderer::end();
  TextRenderer::begin();
  customFontActive_ = false;
  currentFontPath_.clear();
}

String LuaApp::sanitizeFontPath_(const char* raw) const {
  if (!raw) return String();
  String path = String(raw);
  path.trim();
  if (path.isEmpty()) return String();
  if (!path.startsWith("/")) {
    path = String("/system/fonts/") + path;
  }
  if (path == "/system/font.vlw") {
    return path;
  }
  if (!path.startsWith("/system/fonts/")) {
    return String();
  }
  return path;
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
  const String scriptsPrefix = String(kScriptsDir) + "/";
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    String name = f.name();
    if (!name.endsWith(".lua")) continue;

    if (!name.startsWith("/")) {
      name = "/" + name;
    }
    if (!name.startsWith(scriptsPrefix)) {
      int slash = name.lastIndexOf('/');
      String basename = (slash >= 0) ? name.substring(slash + 1) : name;
      name = scriptsPrefix + basename;
    }

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
  String display = scriptDisplayName_;
  if (display.isEmpty()) {
    display = fallbackNameForPath_(scriptPath_);
  }

  const int lineHeight = TextRenderer::lineHeight();
  const int totalHeight = lineHeight * 2 + 8;
  int y = (tft.height() - totalHeight) / 2;
  if (y < 0) y = 0;

  tft.fillScreen(TFT_BLACK);
  TextRenderer::drawCentered(y, "Lua:", TFT_WHITE, TFT_BLACK);
  TextRenderer::drawCentered(y + lineHeight + 6, display, TFT_WHITE, TFT_BLACK);
  delay(700);
}

String LuaApp::determineScriptDisplayName_(const String& path) {
  static const char* kNameKeys[] = {"APP_NAME", "app_name", "AppName"};
  for (const char* key : kNameKeys) {
    lua_getglobal(L_, key);
    if (lua_isstring(L_, -1)) {
      String value = lua_tostring(L_, -1);
      lua_pop(L_, 1);
      value.trim();
      if (!value.isEmpty()) {
        return value;
      }
    } else {
      lua_pop(L_, 1);
    }
  }
  return fallbackNameForPath_(path);
}

String LuaApp::fallbackNameForPath_(const String& path) const {
  if (path.isEmpty()) {
    return String("Unbenannt");
  }
  String name = path;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.substring(slash + 1);
  }
  int dot = name.lastIndexOf('.');
  if (dot > 0) {
    name = name.substring(0, dot);
  }
  if (name.isEmpty()) {
    name = path;
  }
  return name;
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

int LuaApp::lua_line(lua_State* L) {
  int x0 = luaL_checkinteger(L, 1);
  int y0 = luaL_checkinteger(L, 2);
  int x1 = luaL_checkinteger(L, 3);
  int y1 = luaL_checkinteger(L, 4);
  uint32_t color = luaL_optinteger(L, 5, TFT_WHITE);
  tft.drawLine(x0, y0, x1, y1, color);
  return 0;
}

int LuaApp::lua_circle(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int r = luaL_checkinteger(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);
  tft.drawCircle(x, y, r, color);
  return 0;
}

int LuaApp::lua_fillCircle(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  int r = luaL_checkinteger(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);
  tft.fillCircle(x, y, r, color);
  return 0;
}

int LuaApp::lua_triangle(lua_State* L) {
  int x0 = luaL_checkinteger(L, 1);
  int y0 = luaL_checkinteger(L, 2);
  int x1 = luaL_checkinteger(L, 3);
  int y1 = luaL_checkinteger(L, 4);
  int x2 = luaL_checkinteger(L, 5);
  int y2 = luaL_checkinteger(L, 6);
  uint32_t color = luaL_optinteger(L, 7, TFT_WHITE);
  tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

int LuaApp::lua_fillTriangle(lua_State* L) {
  int x0 = luaL_checkinteger(L, 1);
  int y0 = luaL_checkinteger(L, 2);
  int x1 = luaL_checkinteger(L, 3);
  int y1 = luaL_checkinteger(L, 4);
  int x2 = luaL_checkinteger(L, 5);
  int y2 = luaL_checkinteger(L, 6);
  uint32_t color = luaL_optinteger(L, 7, TFT_WHITE);
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
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

int LuaApp::lua_loadFont(lua_State* L) {
  if (!gActiveLuaApp) {
    lua_pushboolean(L, 0);
    return 1;
  }
  const char* raw = luaL_checkstring(L, 1);
  String path = gActiveLuaApp->sanitizeFontPath_(raw);
  if (path.isEmpty()) {
    gActiveLuaApp->lastError_ = "Font-Pfad ungültig";
    lua_pushboolean(L, 0);
    return 1;
  }
  bool ok = gActiveLuaApp->loadTftFont_(path);
  lua_pushboolean(L, ok ? 1 : 0);
  return 1;
}

int LuaApp::lua_unloadFont(lua_State* L) {
  if (!gActiveLuaApp) {
    lua_pushboolean(L, 0);
    return 1;
  }
  gActiveLuaApp->restoreDefaultFont_();
  lua_pushboolean(L, 1);
  return 1;
}

int LuaApp::lua_temperature(lua_State* L) {
  float tempF = temperatureRead();
  float tempC = (tempF - 32.0f) * 0.5555556f;
  lua_pushnumber(L, tempC);
  return 1;
}

int LuaApp::lua_millis(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(millis()));
  return 1;
}

namespace {
const char* kFsWhitelist[] = {
  "/scripts",
  "/slides",
  "/system/fonts"
};

bool pathAllowed(const String& path) {
  if (!path.startsWith("/")) return false;
  for (const char* allowed : kFsWhitelist) {
    if (path.startsWith(allowed)) return true;
  }
  return false;
}

String sanitizeFsPath(const char* raw) {
  if (!raw) return String();
  String path = String(raw);
  path.trim();
  if (path.isEmpty()) return String();
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  // Collapse .. segments simple (no symlinks)
  path.replace("//", "/");
  return path;
}
}

int LuaApp::lua_fs_read(lua_State* L) {
  const char* raw = luaL_checkstring(L, 1);
  String path = sanitizeFsPath(raw);
  if (path.isEmpty() || !pathAllowed(path)) {
    lua_pushnil(L);
    lua_pushstring(L, "Pfad verboten");
    return 2;
  }
  File f = LittleFS.open(path, FILE_READ);
  if (!f) {
    lua_pushnil(L);
    lua_pushstring(L, "Datei fehlt");
    return 2;
  }
  size_t size = f.size();
  String data;
  data.reserve(size);
  while (f.available()) {
    data += static_cast<char>(f.read());
  }
  f.close();
  lua_pushlstring(L, data.c_str(), data.length());
  return 1;
}

int LuaApp::lua_fs_write(lua_State* L) {
  const char* raw = luaL_checkstring(L, 1);
  size_t len = 0;
  const char* content = luaL_checklstring(L, 2, &len);
  String path = sanitizeFsPath(raw);
  if (path.isEmpty() || !pathAllowed(path)) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "Pfad verboten");
    return 2;
  }
  File f = LittleFS.open(path, FILE_WRITE);
  if (!f) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "Schreibfehler");
    return 2;
  }
  size_t written = f.write(reinterpret_cast<const uint8_t*>(content), len);
  f.close();
  if (written != len) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, "Schreibfehler");
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int LuaApp::lua_fs_list(lua_State* L) {
  const char* raw = luaL_optstring(L, 1, "/scripts");
  String path = sanitizeFsPath(raw);
  if (path.isEmpty() || !pathAllowed(path)) {
    lua_pushnil(L);
    lua_pushstring(L, "Pfad verboten");
    return 2;
  }
  File dir = LittleFS.open(path, FILE_READ);
  if (!dir || !dir.isDirectory()) {
    lua_pushnil(L);
    lua_pushstring(L, "Kein Verzeichnis");
    return 2;
  }
  lua_newtable(L);
  int index = 1;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String entry = f.name();
    if (entry.startsWith(path) && path.length() > 1) {
      entry = entry.substring(path.length() + (path.endsWith("/") ? 0 : 1));
    }
    lua_pushinteger(L, index++);
    lua_pushstring(L, entry.c_str());
    lua_settable(L, -3);
  }
  dir.close();
  return 1;
}
