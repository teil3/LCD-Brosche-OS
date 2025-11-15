#include "LuaApp.h"

#include <algorithm>
#include <memory>
#include <cmath>

#include "Core/Gfx.h"
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

int luaToInt(lua_State* L, int idx) {
  double val = luaL_checknumber(L, idx);
  return static_cast<int>(val);
}

LuaApp* gActiveLuaApp = nullptr;
}

void LuaApp::init() {
  #ifdef USB_DEBUG
    Serial.printf("[LuaApp] init() start, free heap: %u bytes\n", ESP.getFreeHeap());
  #endif

  // Clear screen immediately to prevent overlay issues when returning from other apps
  tft.fillScreen(TFT_BLACK);

  ensureScriptsDir_();

  #ifdef USB_DEBUG
    size_t beforeScan = ESP.getFreeHeap();
  #endif

  scanScripts_();

  #ifdef USB_DEBUG
    Serial.printf("[LuaApp] scanScripts() done, free heap: %u bytes (used: %u bytes)\n",
                  ESP.getFreeHeap(), beforeScan - ESP.getFreeHeap());
  #endif

  scriptIndex_ = 0;
  // lastScanMs_ = millis();  // Disabled - periodic scanning causes FD leaks

  // Don't draw anything here - TextRenderer loads font which uses ~9KB!
  // We'll draw status after successful script load

  createVm_();

  #ifdef USB_DEBUG
    Serial.printf("[LuaApp] Before loadScript, free heap: %u bytes\n", ESP.getFreeHeap());
  #endif

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

  // Periodic scanning permanently disabled - causes FD leaks in LittleFS
  // Scripts are only scanned once during init()

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

void LuaApp::resume() {
  refresh();
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

  #ifdef USB_DEBUG
    size_t freeBefore = ESP.getFreeHeap();
    Serial.printf("[LuaApp] Creating VM, free heap: %u bytes\n", freeBefore);
  #endif

  L_ = luaL_newstate();
  if (!L_) {
    #ifdef USB_DEBUG
      Serial.printf("[LuaApp] VM creation FAILED! Free heap: %u bytes\n", ESP.getFreeHeap());
    #endif
    lastError_ = "Lua: kein Speicher";
    vmReady_ = false;
    return;
  }

  #ifdef USB_DEBUG
    size_t freeAfter = ESP.getFreeHeap();
    Serial.printf("[LuaApp] VM created, free heap: %u bytes (used: %u bytes)\n",
                  freeAfter, freeBefore - freeAfter);
  #endif

  lua_atpanic(L_, [](lua_State* L) -> int {
    const char* msg = lua_tostring(L, -1);
    Serial.printf("[Lua] panic: %s\n", msg ? msg : "<nil>");
    return 0;
  });

  // Load only essential Lua libraries to save memory
  // Base library is essential (print, type, pairs, etc.)
  luaL_requiref(L_, LUA_GNAME, luaopen_base, 1); lua_pop(L_, 1);

  // String library is essential for string operations
  luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L_, 1);

  // Math library is essential for calculations
  luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L_, 1);

  // Table and UTF8 libraries removed to save memory (~3-5KB)
  // If needed, they can be re-enabled later
  // luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L_, 1);
  // luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L_, 1);

  #ifdef USB_DEBUG
    Serial.printf("[LuaApp] Libraries loaded, free heap: %u bytes\n", ESP.getFreeHeap());
  #endif

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
  lua_pushcfunction(L_, lua_delay); lua_setfield(L_, -2, "delay");
  lua_pushcfunction(L_, lua_meminfo); lua_setfield(L_, -2, "meminfo");
  lua_pushcfunction(L_, lua_fs_read); lua_setfield(L_, -2, "readFile");
  lua_pushcfunction(L_, lua_fs_write); lua_setfield(L_, -2, "writeFile");
  lua_pushcfunction(L_, lua_fs_list); lua_setfield(L_, -2, "listFiles");
  lua_pushcfunction(L_, lua_gc); lua_setfield(L_, -2, "gc");
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
  scriptDisplayName_.clear();  // Clear cached script name
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
    #ifdef USB_DEBUG
      Serial.printf("[LuaApp] Failed to allocate %u bytes for script. Free heap: %u bytes\n",
                    size, ESP.getFreeHeap());
    #endif
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

  // Clear screen before running setup to avoid artifacts from previous script
  tft.fillScreen(TFT_BLACK);

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

  // Recreate VM to clear all globals from previous script
  createVm_();

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

void LuaApp::refresh() {
  if (!vmReady_ || !scriptLoaded_) return;
  // Clear screen and re-run setup to redraw everything
  tft.fillScreen(TFT_BLACK);
  runSetup_();
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

  // If we already have a custom font loaded, unload it first
  if (customFontActive_) {
    tft.unloadFont();
  }
  // Note: If no custom font was active, TFT still has system font - that's OK!
  // We load our custom font on top of it

  tft.loadFont(chosen.c_str(), LittleFS);
  customFontActive_ = true;
  currentFontPath_ = fullPath;
  return true;
}

void LuaApp::restoreDefaultFont_() {
  if (!customFontActive_) {
    return;
  }
  // Only unload our custom font - don't touch system font!
  tft.unloadFont();
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
  // Don't use TextRenderer here - it loads font which uses ~9KB memory!
  // Use simple TFT text drawing instead
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  String msg = lastError_.isEmpty() ? String("Lua Status") : lastError_;
  tft.drawString(msg, TFT_W / 2, TFT_H / 2);
}

void LuaApp::ensureScriptsDir_() {
  if (!LittleFS.exists(kScriptsDir)) {
    LittleFS.mkdir(kScriptsDir);
  }
}

void LuaApp::scanScripts_() {
  std::vector<String> found;

  File dir = LittleFS.open(kScriptsDir);
  if (!dir) {
    lastError_ = "scripts-Verz. fehlt";
    return;
  }
  if (!dir.isDirectory()) {
    dir.close();
    lastError_ = "scripts-Verz. fehlt";
    return;
  }

  const String scriptsPrefix = String(kScriptsDir) + "/";

  // Iterate through directory - close each file immediately
  File f = dir.openNextFile();
  while (f) {
    bool isDir = f.isDirectory();
    String name = f.name();
    f.close();  // Close IMMEDIATELY after reading properties

    if (!isDir && name.endsWith(".lua")) {
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

    f = dir.openNextFile();
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

  tft.fillScreen(TFT_BLACK);

  // Don't use TextRenderer - it loads font which uses ~9KB memory!
  // Use simple TFT text drawing instead
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  const int lineHeight = 16;  // Approximate height for font 2
  int y = (tft.height() - lineHeight * 2) / 2;
  if (y < 0) y = 0;

  tft.drawString("Lua:", TFT_W / 2, y);
  tft.drawString(display, TFT_W / 2, y + lineHeight + 6);
  delay(700);
}

String LuaApp::determineScriptDisplayName_(const String& path) {
  static const char* kNameKeys[] = {"APP_NAME", "app_name", "AppName"};
  for (const char* key : kNameKeys) {
    lua_getglobal(L_, key);
    #ifdef USB_DEBUG
      Serial.printf("[LuaApp] Checking global '%s': type=%d, isstring=%d\n",
                    key, lua_type(L_, -1), lua_isstring(L_, -1));
    #endif
    if (lua_isstring(L_, -1)) {
      String value = lua_tostring(L_, -1);
      lua_pop(L_, 1);
      value.trim();
      #ifdef USB_DEBUG
        Serial.printf("[LuaApp] Found APP_NAME: '%s'\n", value.c_str());
      #endif
      if (!value.isEmpty()) {
        return value;
      }
    } else {
      lua_pop(L_, 1);
    }
  }
  String fallback = fallbackNameForPath_(path);
  #ifdef USB_DEBUG
    Serial.printf("[LuaApp] Using fallback name: '%s'\n", fallback.c_str());
  #endif
  return fallback;
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
  int x = luaToInt(L, 1);
  int y = luaToInt(L, 2);
  int w = luaToInt(L, 3);
  int h = luaToInt(L, 4);
  uint32_t color = luaL_optinteger(L, 5, TFT_WHITE);
  tft.fillRect(x, y, w, h, color);
  return 0;
}

int LuaApp::lua_line(lua_State* L) {
  int x0 = luaToInt(L, 1);
  int y0 = luaToInt(L, 2);
  int x1 = luaToInt(L, 3);
  int y1 = luaToInt(L, 4);
  uint32_t color = luaL_optinteger(L, 5, TFT_WHITE);
  tft.drawLine(x0, y0, x1, y1, color);
  return 0;
}

int LuaApp::lua_circle(lua_State* L) {
  int x = luaToInt(L, 1);
  int y = luaToInt(L, 2);
  int r = luaToInt(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);
  tft.drawCircle(x, y, r, color);
  return 0;
}

int LuaApp::lua_fillCircle(lua_State* L) {
  int x = luaToInt(L, 1);
  int y = luaToInt(L, 2);
  int r = luaToInt(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);
  tft.fillCircle(x, y, r, color);
  return 0;
}

int LuaApp::lua_triangle(lua_State* L) {
  int x0 = luaToInt(L, 1);
  int y0 = luaToInt(L, 2);
  int x1 = luaToInt(L, 3);
  int y1 = luaToInt(L, 4);
  int x2 = luaToInt(L, 5);
  int y2 = luaToInt(L, 6);
  uint32_t color = luaL_optinteger(L, 7, TFT_WHITE);
  tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

int LuaApp::lua_fillTriangle(lua_State* L) {
  int x0 = luaToInt(L, 1);
  int y0 = luaToInt(L, 2);
  int x1 = luaToInt(L, 3);
  int y1 = luaToInt(L, 4);
  int x2 = luaToInt(L, 5);
  int y2 = luaToInt(L, 6);
  uint32_t color = luaL_optinteger(L, 7, TFT_WHITE);
  tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

int LuaApp::lua_text(lua_State* L) {
  int x = luaL_checkinteger(L, 1);
  int y = luaL_checkinteger(L, 2);
  const char* text = luaL_checkstring(L, 3);
  uint32_t color = luaL_optinteger(L, 4, TFT_WHITE);

  tft.setTextDatum(MC_DATUM);

  // Check if background color was provided (5th parameter)
  if (lua_gettop(L) >= 5) {
    // Background color provided - draw clearing rectangle
    uint32_t bg = luaL_checkinteger(L, 5);

    // Calculate text bounds to draw background rectangle
    int16_t textWidth = tft.textWidth(text);
    int16_t textHeight = tft.fontHeight();

    // Draw background rectangle slightly larger than text
    int16_t rectX = x - textWidth / 2 - 2;
    int16_t rectY = y - textHeight / 2 - 1;
    int16_t rectW = textWidth + 4;
    int16_t rectH = textHeight + 2;
    tft.fillRect(rectX, rectY, rectW, rectH, bg);

    // Draw text on top with background
    tft.setTextColor(color, bg);
    tft.drawString(text, x, y);
  } else {
    // No background - draw text only (transparent)
    tft.setTextColor(color);
    tft.drawString(text, x, y);
  }

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

int LuaApp::lua_delay(lua_State* L) {
  int ms = luaL_checkinteger(L, 1);
  if (ms < 0) ms = 0;
  if (ms > 10000) ms = 10000;  // Safety limit: max 10 seconds
  delay(ms);
  return 0;
}

int LuaApp::lua_meminfo(lua_State* L) {
  // Returns a table with memory information
  lua_newtable(L);

  // Free heap
  size_t freeHeap = ESP.getFreeHeap();
  lua_pushinteger(L, static_cast<lua_Integer>(freeHeap));
  lua_setfield(L, -2, "free");

  // Total heap
  size_t totalHeap = ESP.getHeapSize();
  lua_pushinteger(L, static_cast<lua_Integer>(totalHeap));
  lua_setfield(L, -2, "total");

  // Used heap
  lua_pushinteger(L, static_cast<lua_Integer>(totalHeap - freeHeap));
  lua_setfield(L, -2, "used");

  // Largest free block
  size_t largestBlock = ESP.getMaxAllocHeap();
  lua_pushinteger(L, static_cast<lua_Integer>(largestBlock));
  lua_setfield(L, -2, "largest_block");

  // PSRAM if available
  #ifdef BOARD_HAS_PSRAM
    if (psramFound()) {
      lua_pushinteger(L, static_cast<lua_Integer>(ESP.getFreePsram()));
      lua_setfield(L, -2, "psram_free");
      lua_pushinteger(L, static_cast<lua_Integer>(ESP.getPsramSize()));
      lua_setfield(L, -2, "psram_total");
    }
  #endif

  return 1;
}

int LuaApp::lua_gc(lua_State* L) {
  // Force garbage collection using global lua_gc function
  ::lua_gc(L, LUA_GCCOLLECT, 0);
  return 0;
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
  if (!dir) {
    lua_pushnil(L);
    lua_pushstring(L, "Kein Verzeichnis");
    return 2;
  }
  if (!dir.isDirectory()) {
    dir.close();
    lua_pushnil(L);
    lua_pushstring(L, "Kein Verzeichnis");
    return 2;
  }

  lua_newtable(L);
  int index = 1;

  File f = dir.openNextFile();
  while (f) {
    String entry = f.name();
    f.close();  // Close IMMEDIATELY after getting name

    if (entry.startsWith(path) && path.length() > 1) {
      entry = entry.substring(path.length() + (path.endsWith("/") ? 0 : 1));
    }
    lua_pushinteger(L, index++);
    lua_pushstring(L, entry.c_str());
    lua_settable(L, -3);

    f = dir.openNextFile();
  }

  dir.close();
  return 1;
}
