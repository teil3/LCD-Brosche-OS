#include "AppAPI.h"
#include "TextRenderer.h"
#include "Gfx.h"
#include <Arduino.h>

/**
 * Shared AppAPI implementation used by both StaticPluginApp and PluginApp
 * This avoids duplicate symbol definitions when both .cpp files are included.
 */

namespace AppAPIImpl {

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

// Global shared AppAPI instance
AppAPI coreAPI = {
  .version = APP_API_VERSION,
  .tft = &tft,
  .drawCentered = api_drawCentered,
  .lineHeight = api_lineHeight,
  .logInfo = api_logInfo,
  .logError = api_logError,
  .millis = api_millis,
  .delay = api_delay
};

} // namespace AppAPIImpl
