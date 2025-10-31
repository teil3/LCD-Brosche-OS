/**
 * HelloWorld Plugin - Standalone version for dynamic loading
 *
 * This is compiled separately as a position-independent binary.
 * It uses ONLY the AppAPI interface - no direct Arduino/ESP-IDF dependencies.
 */

// Minimal type definitions (we can't include Arduino.h in plugin build)
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef long int32_t;

// Forward declare TFT_eSPI (we only use the pointer, don't need full definition)
class TFT_eSPI;

// AppAPI definition (must match Core/AppAPI.h exactly!)
struct AppAPI {
  uint32_t version;
  TFT_eSPI* tft;
  void (*drawCentered)(int32_t y, const char* text, uint32_t fg, uint32_t bg);
  int32_t (*lineHeight)();
  void (*logInfo)(const char* msg);
  void (*logError)(const char* msg);
  uint32_t (*millis)();
  void (*delay)(uint32_t ms);
};

typedef struct {
  void (*init)(const AppAPI* api);
  void (*onActivate)();
  void (*tick)(uint32_t delta_ms);
  void (*draw)();
  void (*onButton)(uint8_t index, uint8_t event);
  void (*shutdown)();
  const char* (*getName)();
} PluginAppVTable;

// Plugin state
static const AppAPI* api = nullptr;
static uint32_t frameCount = 0;

// Simple integer to string conversion
static void int_to_str(uint32_t num, char* buf, int size) {
  if (size < 2) return;

  if (num == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  int i = 0;
  uint32_t temp = num;

  // Count digits
  int digits = 0;
  while (temp > 0) {
    digits++;
    temp /= 10;
  }

  if (digits >= size) digits = size - 1;

  // Write digits backwards
  buf[digits] = '\0';
  i = digits - 1;
  while (num > 0 && i >= 0) {
    buf[i] = '0' + (num % 10);
    num /= 10;
    i--;
  }
}

// String concatenation
static void str_concat(char* dest, const char* src, int max_len) {
  int dest_len = 0;
  while (dest[dest_len] != '\0' && dest_len < max_len - 1) {
    dest_len++;
  }

  int i = 0;
  while (src[i] != '\0' && dest_len < max_len - 1) {
    dest[dest_len++] = src[i++];
  }
  dest[dest_len] = '\0';
}

extern "C" {

void plugin_init(const AppAPI* core_api) {
  api = core_api;
  if (api && api->logInfo) {
    api->logInfo("[HelloWorldPlugin] Init from .bin file!");
  }
}

void plugin_onActivate() {
  frameCount = 0;
  if (api && api->logInfo) {
    api->logInfo("[HelloWorldPlugin] Activated");
  }
}

void plugin_tick(uint32_t delta_ms) {
  frameCount++;
}

void plugin_draw() {
  if (!api || !api->tft || !api->drawCentered || !api->lineHeight) {
    return;
  }

  // Colors
  const uint32_t TFT_BLACK = 0x0000;
  const uint32_t TFT_WHITE = 0xFFFF;
  const uint32_t TFT_GREEN = 0x07E0;
  const uint32_t TFT_CYAN = 0x07FF;

  // Draw text (colors as uint32_t)
  int32_t y = 70;
  int32_t line = api->lineHeight();

  api->drawCentered(y, "Loaded from", TFT_WHITE, TFT_BLACK);
  api->drawCentered(y + line, ".bin file!", TFT_GREEN, TFT_BLACK);

  // Draw frame counter
  char buf[32] = "Frames: ";
  char num_buf[16];
  int_to_str(frameCount, num_buf, sizeof(num_buf));
  str_concat(buf, num_buf, sizeof(buf));

  api->drawCentered(y + line * 3, buf, TFT_CYAN, TFT_BLACK);
}

void plugin_onButton(uint8_t index, uint8_t event) {
  if (!api || !api->logInfo) return;

  char buf[64] = "[HelloWorldPlugin] Button ";
  char num[4];
  int_to_str(index, num, sizeof(num));
  str_concat(buf, num, sizeof(buf));
  str_concat(buf, " Event ", sizeof(buf));
  int_to_str(event, num, sizeof(num));
  str_concat(buf, num, sizeof(buf));

  api->logInfo(buf);
}

void plugin_shutdown() {
  if (api && api->logInfo) {
    api->logInfo("[HelloWorldPlugin] Shutdown");
  }
}

const char* plugin_getName() {
  return "Plugin.bin";
}

// VTable exported at known section
__attribute__((section(".plugin_vtable"), used))
const PluginAppVTable plugin_vtable = {
  .init = plugin_init,
  .onActivate = plugin_onActivate,
  .tick = plugin_tick,
  .draw = plugin_draw,
  .onButton = plugin_onButton,
  .shutdown = plugin_shutdown,
  .getName = plugin_getName
};

} // extern "C"
