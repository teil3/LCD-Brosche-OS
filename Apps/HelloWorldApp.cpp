#include "Core/AppAPI.h"

// Global API pointer (set by core during init)
static const AppAPI* api = nullptr;

// App state
static uint32_t frameCount = 0;

// Plugin implementation
extern "C" {

void plugin_init(const AppAPI* core_api) {
  api = core_api;
  if (api) {
    api->logInfo("[HelloWorldApp] Initialized");
  }
}

void plugin_onActivate() {
  frameCount = 0;
  if (api) {
    api->logInfo("[HelloWorldApp] Activated");
  }
}

void plugin_tick(uint32_t delta_ms) {
  frameCount++;
}

void plugin_draw() {
  if (!api || !api->tft) return;

  // Clear screen
  api->tft->fillScreen(TFT_BLACK);

  // Draw centered text
  int16_t y = 80;
  int16_t line = api->lineHeight();

  api->drawCentered(y, "Hello from", 0xFFFF, 0x0000);
  api->drawCentered(y + line, "Plugin App!", 0x07E0, 0x0000);

  // Draw frame counter
  char buf[32];
  snprintf(buf, sizeof(buf), "Frames: %lu", (unsigned long)frameCount);
  api->drawCentered(y + line * 3, buf, 0x07FF, 0x0000);
}

void plugin_onButton(uint8_t index, uint8_t event) {
  if (!api) return;

  char buf[64];
  snprintf(buf, sizeof(buf), "[HelloWorldApp] Button %d Event %d", index, event);
  api->logInfo(buf);
}

void plugin_shutdown() {
  if (api) {
    api->logInfo("[HelloWorldApp] Shutdown");
  }
}

const char* plugin_getName() {
  return "Hello World";
}

// Export VTable
const PluginAppVTable helloworld_vtable = {
  .init = plugin_init,
  .onActivate = plugin_onActivate,
  .tick = plugin_tick,
  .draw = plugin_draw,
  .onButton = plugin_onButton,
  .shutdown = plugin_shutdown,
  .getName = plugin_getName
};

} // extern "C"

// Make VTable accessible to main program
extern "C" const PluginAppVTable* getHelloWorldVTable() {
  return &helloworld_vtable;
}

