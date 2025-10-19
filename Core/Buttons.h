#ifndef CORE_BUTTONS_H
#define CORE_BUTTONS_H

#include <Arduino.h>
#include "Config.h"
#include "App.h"

struct ButtonCfg { uint8_t pin; bool pullup; };

class ButtonState {
public:
  explicit ButtonState(ButtonCfg cfg) : cfg_(cfg) {}
  void begin();
  BtnEvent poll();
private:
  ButtonCfg cfg_;
  bool lastLevel_ = true;
  uint32_t lastChange_ = 0;
  uint8_t  clickCount_ = 0;
  bool pressed_ = false;
  uint32_t pressStart_ = 0;
  bool longFired_ = false;
};

#endif // CORE_BUTTONS_H

