#pragma once

#ifndef CORE_SETUPMENU_H
#define CORE_SETUPMENU_H

#include <Arduino.h>

// Einfache System-Overlay-Navigation mit vier Einträgen.
class SetupMenu {
public:
  enum class Item : uint8_t {
    UsbBleTransfer = 0,
    SdTransfer = 1,
    SourceSelection = 2,
    Exit = 3,
    Count
  };

  void show();
  void hide();
  bool isVisible() const { return visible_; }
  Item currentItem() const;
  void next();
  void draw(bool force = false);
  void showStatus(const String& text, uint32_t duration_ms = 1200);

private:
  bool visible_ = false;
  uint8_t selected_ = 0;
  bool dirty_ = false;
  String statusText_;
  uint32_t statusUntil_ = 0;
};

#endif // CORE_SETUPMENU_H
