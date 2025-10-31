#include "PluginApp.h"
#include "TextRenderer.h"
#include "Gfx.h"
#include <esp_heap_caps.h>

// Forward declarations for API functions
namespace {

void api_drawCentered(int16_t y, const char* text, uint32_t fg, uint32_t bg) {
  TextRenderer::drawCentered(y, String(text), fg, bg);
}

int16_t api_lineHeight() {
  return TextRenderer::lineHeight();
}

void api_logInfo(const char* msg) {
  Serial.print("[Plugin] ");
  Serial.println(msg);
}

void api_logError(const char* msg) {
  Serial.print("[Plugin ERROR] ");
  Serial.println(msg);
}

uint32_t api_millis() {
  return millis();
}

void api_delay(uint32_t ms) {
  delay(ms);
}

} // namespace

// Initialize static core API
AppAPI PluginApp::coreAPI_ = {
  .version = APP_API_VERSION,
  .tft = &tft,
  .drawCentered = api_drawCentered,
  .lineHeight = api_lineHeight,
  .logInfo = api_logInfo,
  .logError = api_logError,
  .millis = api_millis,
  .delay = api_delay
};

PluginApp::PluginApp(const char* binPath) : binPath_(binPath) {
  Serial.printf("[PluginApp] Constructor: %s\n", binPath);
  loaded_ = loadPlugin_();
  if (!loaded_) {
    Serial.printf("[PluginApp] Failed to load: %s\n", binPath);
    appName_ = "Failed Plugin";
  }
}

PluginApp::~PluginApp() {
  unloadPlugin_();
}

bool PluginApp::loadPlugin_() {
  Serial.printf("[PluginApp] Loading from: %s\n", binPath_.c_str());

  // Check if file exists
  if (!LittleFS.exists(binPath_.c_str())) {
    Serial.printf("[PluginApp] File not found: %s\n", binPath_.c_str());
    return false;
  }

  // Open file
  File f = LittleFS.open(binPath_.c_str(), FILE_READ);
  if (!f) {
    Serial.printf("[PluginApp] Failed to open: %s\n", binPath_.c_str());
    return false;
  }

  pluginSize_ = f.size();
  Serial.printf("[PluginApp] File size: %u bytes\n", (unsigned)pluginSize_);

  if (pluginSize_ == 0 || pluginSize_ > 128 * 1024) {
    Serial.printf("[PluginApp] Invalid size: %u\n", (unsigned)pluginSize_);
    f.close();
    return false;
  }

  // Allocate executable memory (IRAM)
  pluginMemory_ = heap_caps_malloc(pluginSize_, MALLOC_CAP_EXEC);
  if (!pluginMemory_) {
    Serial.printf("[PluginApp] Failed to allocate %u bytes of IRAM\n", (unsigned)pluginSize_);
    f.close();
    return false;
  }

  // Load binary into memory
  size_t bytesRead = f.read((uint8_t*)pluginMemory_, pluginSize_);
  f.close();

  if (bytesRead != pluginSize_) {
    Serial.printf("[PluginApp] Read mismatch: %u != %u\n", (unsigned)bytesRead, (unsigned)pluginSize_);
    unloadPlugin_();
    return false;
  }

  Serial.printf("[PluginApp] Loaded to IRAM: %p\n", pluginMemory_);

  // Find VTable in binary
  if (!findVTable_()) {
    Serial.println("[PluginApp] Failed to find VTable");
    unloadPlugin_();
    return false;
  }

  // Call plugin init
  if (vtable_.init) {
    vtable_.init(&coreAPI_);
  }

  // Get plugin name
  if (vtable_.getName) {
    appName_ = String(vtable_.getName());
  } else {
    appName_ = "Unknown Plugin";
  }

  Serial.printf("[PluginApp] Successfully loaded: %s\n", appName_.c_str());
  return true;
}

void PluginApp::unloadPlugin_() {
  if (pluginMemory_) {
    heap_caps_free(pluginMemory_);
    pluginMemory_ = nullptr;
  }
  pluginSize_ = 0;
  loaded_ = false;
}

bool PluginApp::findVTable_() {
  if (!pluginMemory_ || pluginSize_ < sizeof(PluginAppVTable)) {
    return false;
  }

  // Strategy 1: Look for VTable at known offset (beginning of binary)
  // For now, assume VTable is at start of binary
  PluginAppVTable* candidate = (PluginAppVTable*)pluginMemory_;

  // Sanity check: all function pointers should be non-null and within reasonable range
  uintptr_t base = (uintptr_t)pluginMemory_;
  uintptr_t end = base + pluginSize_;

  auto isValidPtr = [base, end](void* ptr) -> bool {
    if (!ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    return (addr >= base && addr < end);
  };

  if (isValidPtr((void*)candidate->init) &&
      isValidPtr((void*)candidate->draw) &&
      isValidPtr((void*)candidate->getName)) {
    memcpy(&vtable_, candidate, sizeof(PluginAppVTable));
    Serial.printf("[PluginApp] Found VTable at offset 0\n");
    return true;
  }

  // Strategy 2: Search through binary for VTable signature
  // TODO: Implement if Strategy 1 doesn't work

  Serial.println("[PluginApp] VTable not found");
  return false;
}

const char* PluginApp::name() const {
  return appName_.c_str();
}

void PluginApp::init() {
  if (!loaded_ || !vtable_.onActivate) return;
  vtable_.onActivate();
}

void PluginApp::tick(uint32_t delta_ms) {
  if (!loaded_ || !vtable_.tick) return;
  vtable_.tick(delta_ms);
}

void PluginApp::onButton(uint8_t index, BtnEvent e) {
  if (!loaded_ || !vtable_.onButton) return;
  vtable_.onButton(index, static_cast<uint8_t>(e));
}

void PluginApp::draw() {
  if (!loaded_ || !vtable_.draw) return;
  vtable_.draw();
}

void PluginApp::shutdown() {
  if (!loaded_ || !vtable_.shutdown) return;
  vtable_.shutdown();
}
