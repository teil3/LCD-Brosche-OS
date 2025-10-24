#include "AppManager.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "TextRenderer.h"

extern TFT_eSPI tft; // aus Gfx.cpp

void AppManager::add(App* a) { apps_.push_back(a); }
void AppManager::begin() { if (!apps_.empty()) setActive(0); }

void AppManager::setActive(int i) {
  if (i < 0 || i >= (int)apps_.size()) return;
  if (active_ >= 0) apps_[active_]->shutdown();
  active_ = i;
  if (Serial) {
    Serial.printf("[APP] activate %s (%d/%d)\n", apps_[active_]->name(), active_ + 1, (int)apps_.size());
  }
  tft.fillScreen(TFT_BLACK);
  int16_t textY = (tft.height() - TextRenderer::lineHeight()) / 2;
  if (textY < 0) textY = 0;
  TextRenderer::drawCentered(textY, String(apps_[active_]->name()), TFT_WHITE, TFT_BLACK);
  delay(1000);
  apps_[active_]->init();
}

void AppManager::next() { if (!apps_.empty()) setActive((active_+1)%apps_.size()); }
void AppManager::prev() { if (!apps_.empty()) setActive((active_+apps_.size()-1)%apps_.size()); }

App* AppManager::activeApp() const {
  if (active_ < 0 || active_ >= (int)apps_.size()) {
    return nullptr;
  }
  return apps_[active_];
}

void AppManager::dispatchBtn(uint8_t idx, BtnEvent e) { if (active_>=0) apps_[active_]->onButton(idx,e); }
void AppManager::tick(uint32_t dt) { if (active_>=0) apps_[active_]->tick(dt); }
void AppManager::draw() { if (active_>=0) apps_[active_]->draw(); }
