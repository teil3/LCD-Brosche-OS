#include "Buttons.h"

#ifdef USB_DEBUG
static const char* eventName(BtnEvent e) {
  switch (e) {
    case BtnEvent::Single: return "Single";
    case BtnEvent::Double: return "Double";
    case BtnEvent::Triple: return "Triple";
    case BtnEvent::Long:   return "Long";
    default:               return "None";
  }
}
#endif

void ButtonState::begin() {
  pinMode(cfg_.pin, cfg_.pullup ? INPUT_PULLUP : INPUT);
  int raw = digitalRead(cfg_.pin);
  lastLevel_ = cfg_.pullup ? (raw == HIGH) : (raw != LOW);
  lastChange_ = millis();
}

BtnEvent ButtonState::poll() {
  int raw = digitalRead(cfg_.pin);
  bool level = raw;
  if (cfg_.pullup) level = (raw == HIGH); // idle=true
  uint32_t now = millis();
  BtnEvent evt = BtnEvent::None;

  // Edge + Debounce
  if (level != lastLevel_ && (now - lastChange_) >= DEBOUNCE_MS) {
    #ifdef USB_DEBUG
      Serial.printf("[BTN DBG] pin%u raw=%d logical=%d\n", cfg_.pin, raw, level);
    #endif
    lastLevel_ = level;
    lastChange_ = now;
    bool isDown = !level; // pullup: LOW=pressed
    if (isDown) { pressed_ = true; pressStart_ = now; longFired_ = false; }
    else { pressed_ = false; if (!longFired_) clickCount_++; }
  }

  // Long
  if (pressed_ && !longFired_ && (now - pressStart_) >= LONG_MS) {
    longFired_ = true; clickCount_ = 0;
    #ifdef USB_DEBUG
      Serial.printf("[BTN] pin%u %s\n", cfg_.pin, eventName(BtnEvent::Long));
    #endif
    return BtnEvent::Long;
  }

  // Muster (wenn losgelassen)
  if (!pressed_ && clickCount_ > 0) {
    uint32_t gap = (clickCount_ == 1) ? DOUBLE_GAP_MS : TRIPLE_GAP_MS;
    if ((now - lastChange_) >= gap) {
      BtnEvent out = BtnEvent::Single;
      if (clickCount_ >= 3) out = BtnEvent::Triple;
      else if (clickCount_ == 2) out = BtnEvent::Double;
      clickCount_ = 0;
      #ifdef USB_DEBUG
        Serial.printf("[BTN] pin%u %s\n", cfg_.pin, eventName(out));
      #endif
      return out;
    }
  }
  return evt;
}
