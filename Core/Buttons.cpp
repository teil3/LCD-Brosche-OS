#include "Buttons.h"

void ButtonState::begin() {
  pinMode(cfg_.pin, cfg_.pullup ? INPUT_PULLUP : INPUT);
}

BtnEvent ButtonState::poll() {
  bool level = digitalRead(cfg_.pin);
  if (cfg_.pullup) level = (level == HIGH); // idle=true
  uint32_t now = millis();
  BtnEvent evt = BtnEvent::None;

  // Edge + Debounce
  if (level != lastLevel_ && (now - lastChange_) >= DEBOUNCE_MS) {
    lastLevel_ = level;
    lastChange_ = now;
    bool isDown = !level; // pullup: LOW=pressed
    if (isDown) { pressed_ = true; pressStart_ = now; longFired_ = false; }
    else { pressed_ = false; if (!longFired_) clickCount_++; }
  }

  // Long
  if (pressed_ && !longFired_ && (now - pressStart_) >= LONG_MS) {
    longFired_ = true; clickCount_ = 0; return BtnEvent::Long;
  }

  // Muster (wenn losgelassen)
  if (!pressed_ && clickCount_ > 0) {
    uint32_t gap = (clickCount_ == 1) ? DOUBLE_GAP_MS : TRIPLE_GAP_MS;
    if ((now - lastChange_) >= gap) {
      BtnEvent out = BtnEvent::Single;
      if (clickCount_ >= 3) out = BtnEvent::Triple;
      else if (clickCount_ == 2) out = BtnEvent::Double;
      clickCount_ = 0;
      return out;
    }
  }
  return evt;
}

