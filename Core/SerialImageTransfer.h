#pragma once

#ifndef CORE_SERIAL_IMAGE_TRANSFER_H
#define CORE_SERIAL_IMAGE_TRANSFER_H

#include <stdint.h>
#include <stddef.h>

namespace SerialImageTransfer {

enum class EventType : uint8_t {
  Started = 0,
  Completed,
  Error,
  Aborted,
};

struct Event {
  EventType type = EventType::Started;
  size_t size = 0;
  char filename[64];
  char message[96];
};

void begin();
void tick();
bool pollEvent(Event* outEvent);
bool isTransferActive();
size_t bytesExpected();
size_t bytesReceived();
void setTransferEnabled(bool enabled);
bool transferEnabled();

} // namespace SerialImageTransfer

#endif // CORE_SERIAL_IMAGE_TRANSFER_H

