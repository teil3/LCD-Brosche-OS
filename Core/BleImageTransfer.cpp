#include "BleImageTransfer.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLE2902.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <cstdlib>

#include "Storage.h"

#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {

constexpr const char* kServiceUuid       = "12345678-1234-1234-1234-1234567890ab";
constexpr const char* kChunkCharUuid     = "abcd0001-1234-1234-1234-1234567890ab";
constexpr const char* kControlCharUuid   = "abcd0002-1234-1234-1234-1234567890ab";
constexpr size_t      kMaxImageSize      = 320 * 1024;   // 320 KB safety cap
constexpr uint32_t    kTransferTimeoutMs = 6000;         // abort after 6s idle
constexpr size_t      kFilenameCapacity  = sizeof(BleImageTransfer::Event::filename);

struct TransferSession {
  bool active = false;
  File file;
  size_t expected = 0;
  size_t received = 0;
  size_t lastNotified = 0;
  uint32_t startedAt = 0;
  uint32_t lastActivity = 0;
  char filename[kFilenameCapacity];
};

QueueHandle_t gEventQueue = nullptr;
TransferSession gSession;

BLECharacteristic* gControlChar = nullptr;
BLECharacteristic* gChunkChar = nullptr;
bool gTransfersEnabled = false;

void ensureQueue() {
  if (!gEventQueue) {
    gEventQueue = xQueueCreate(8, sizeof(BleImageTransfer::Event));
  }
}

void postEvent(BleImageTransfer::EventType type,
               const char* filename,
               size_t size,
               const char* message) {
  ensureQueue();
  if (!gEventQueue) return;

  BleImageTransfer::Event evt{};
  evt.type = type;
  evt.size = size;

  evt.filename[0] = '\0';
  if (filename && filename[0]) {
    std::snprintf(evt.filename, sizeof(evt.filename), "%s", filename);
  }

  evt.message[0] = '\0';
  if (message && message[0]) {
    std::snprintf(evt.message, sizeof(evt.message), "%s", message);
  }

  xQueueSend(gEventQueue, &evt, 0);
}

void sendStatus(const char* text) {
  if (!gControlChar || !text) return;
  gControlChar->setValue((uint8_t*)text, std::strlen(text));
  gControlChar->notify();
}

void cleanupFileOnError() {
  if (!gSession.filename[0]) return;
  String path = String(kFlashSlidesDir) + "/" + gSession.filename;
  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
}

void resetSession() {
  if (gSession.file) {
    gSession.file.close();
  }
  gSession.file = File();
  gSession.active = false;
  gSession.expected = 0;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = 0;
  gSession.lastActivity = 0;
  std::memset(gSession.filename, 0, sizeof(gSession.filename));
}

bool validateSize(size_t sz) {
  return sz > 0 && sz <= kMaxImageSize;
}

void sanitizeFilename(const std::string& requested, char* out, size_t outLen) {
  if (!out || outLen == 0) return;

  const char* defaultName = "ble_image.jpg";
  size_t pos = 0;
  for (char ch : requested) {
    if (pos >= outLen - 1) break;
    if (ch >= 'A' && ch <= 'Z') {
      out[pos++] = ch + 32; // lower-case
    } else if ((ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               ch == '-' || ch == '_' || ch == '.') {
      out[pos++] = ch;
    } else if (ch == ' ') {
      out[pos++] = '_';
    }
  }
  out[pos] = '\0';

  if (pos == 0) {
    std::snprintf(out, outLen, "%s", defaultName);
    return;
  }

  if (out[0] == '.') {
    std::memmove(out, out + 1, pos);
    --pos;
    out[pos] = '\0';
  }

  const char* ext = ".jpg";
  const char* dot = std::strrchr(out, '.');
  if (!dot) {
    size_t remaining = outLen - 1 - pos;
    if (remaining >= std::strlen(ext)) {
      std::strcat(out, ext);
    } else {
      // truncate & append extension safely
      size_t copyable = std::min(remaining, std::strlen(ext));
      std::strncat(out, ext, copyable);
    }
  }
}

void generateUniqueFilename(char* out, size_t outLen) {
  uint32_t now = millis();
  std::snprintf(out, outLen, "ble_%08lu.jpg", static_cast<unsigned long>(now));

  String path = String(kFlashSlidesDir) + "/" + out;
  uint16_t attempt = 1;
  while (LittleFS.exists(path) && attempt < 1000) {
    std::snprintf(out, outLen, "ble_%08lu_%u.jpg",
                  static_cast<unsigned long>(now), attempt);
    path = String(kFlashSlidesDir) + "/" + out;
    ++attempt;
  }
}

void ensureUniqueOnFs(char* nameBuf, size_t bufLen) {
  if (!nameBuf || !nameBuf[0]) {
    generateUniqueFilename(nameBuf, bufLen);
    return;
  }

  String base(nameBuf);
  String ext(".jpg");
  int dotIdx = base.lastIndexOf('.');
  if (dotIdx >= 0) {
    ext = base.substring(dotIdx);
    base = base.substring(0, dotIdx);
  }

  String path = String(kFlashSlidesDir) + "/" + String(nameBuf);
  if (!LittleFS.exists(path)) {
    return;
  }

  for (int i = 1; i < 1000; ++i) {
    String candidate = base + "-" + String(i) + ext;
    if (candidate.length() >= static_cast<int>(bufLen)) {
      continue;
    }
    path = String(kFlashSlidesDir) + "/" + candidate;
    if (!LittleFS.exists(path)) {
      std::snprintf(nameBuf, bufLen, "%s", candidate.c_str());
      return;
    }
  }

  generateUniqueFilename(nameBuf, bufLen);
}

bool beginTransfer(size_t size, const std::string& requestedName) {
  if (!gTransfersEnabled) {
    sendStatus("ERR:DISABLED");
    postEvent(BleImageTransfer::EventType::Error, "", size, "BLE-Modus nicht aktiv");
    return false;
  }
  if (!ensureFlashSlidesDir()) {
    sendStatus("ERR:FLASHDIR");
    postEvent(BleImageTransfer::EventType::Error, "", size, "LittleFS init fehlgeschlagen");
    return false;
  }

  if (!validateSize(size)) {
    sendStatus("ERR:SIZE");
    postEvent(BleImageTransfer::EventType::Error, "", size, "Ungültige Dateigröße");
    return false;
  }

  if (gSession.active) {
    cleanupFileOnError();
    resetSession();
  }

  char filename[kFilenameCapacity];
  filename[0] = '\0';
  if (!requestedName.empty()) {
    sanitizeFilename(requestedName, filename, sizeof(filename));
  }
  if (!filename[0]) {
    generateUniqueFilename(filename, sizeof(filename));
  }

  ensureUniqueOnFs(filename, sizeof(filename));

  String path = String(kFlashSlidesDir) + "/" + filename;
  gSession.file = LittleFS.open(path.c_str(), FILE_WRITE);
  if (!gSession.file) {
    sendStatus("ERR:OPEN");
    postEvent(BleImageTransfer::EventType::Error, filename, size, "Datei konnte nicht angelegt werden");
    return false;
  }

  gSession.active = true;
  gSession.expected = size;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = millis();
  gSession.lastActivity = gSession.startedAt;
  std::snprintf(gSession.filename, sizeof(gSession.filename), "%s", filename);

  char status[80];
  std::snprintf(status, sizeof(status), "OK:START:%s:%lu",
                gSession.filename,
                static_cast<unsigned long>(size));
  sendStatus(status);

  char msg[96];
  std::snprintf(msg, sizeof(msg), "Empfange %s (%lu B)",
                gSession.filename,
                static_cast<unsigned long>(size));
  postEvent(BleImageTransfer::EventType::Started, gSession.filename, size, msg);

  Serial.printf("[BLE] START %s (%lu bytes)\n", gSession.filename, (unsigned long)size);
  return true;
}

void abortTransfer(const char* reason, BleImageTransfer::EventType evtType) {
  if (!gSession.active) return;

  cleanupFileOnError();
  char fname[sizeof(gSession.filename)];
  std::snprintf(fname, sizeof(fname), "%s", gSession.filename);
  size_t received = gSession.received;

  resetSession();

  if (reason) {
    char status[64];
    std::snprintf(status, sizeof(status), "ERR:%s", reason);
    sendStatus(status);
  }

  postEvent(evtType, fname, received, reason ? reason : "");
  Serial.printf("[BLE] ABORT (%s) after %lu bytes\n",
                reason ? reason : "no-reason",
                static_cast<unsigned long>(received));
}

void completeTransfer() {
  char fname[sizeof(gSession.filename)];
  std::snprintf(fname, sizeof(fname), "%s", gSession.filename);
  size_t received = gSession.received;

  gSession.file.close();
  gSession.file = File();
  gSession.active = false;
  gSession.expected = 0;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = 0;
  gSession.lastActivity = 0;
  std::memset(gSession.filename, 0, sizeof(gSession.filename));

  char status[64];
  std::snprintf(status, sizeof(status), "OK:END:%s", fname);
  sendStatus(status);

  postEvent(BleImageTransfer::EventType::Completed, fname, received, "Übertragung abgeschlossen");
  Serial.printf("[BLE] COMPLETE %s (%lu bytes)\n",
                fname,
                static_cast<unsigned long>(received));
}

class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    String value = characteristic->getValue();
    if (value.length() == 0) return;

    if (value.startsWith("START")) {
      int first = value.indexOf(':');
      if (first < 0 || first + 1 >= value.length()) {
        sendStatus("ERR:STRTFMT");
        return;
      }
      int second = value.indexOf(':', first + 1);
      String sizePart = (second < 0) ? value.substring(first + 1)
                                     : value.substring(first + 1, second);
      size_t sz = strtoul(sizePart.c_str(), nullptr, 10);
      String namePart = (second >= 0 && second + 1 < value.length())
                        ? value.substring(second + 1)
                        : String();
      beginTransfer(sz, std::string(namePart.c_str()));
    } else if (value.equals("END")) {
      if (!gSession.active) {
        sendStatus("ERR:NOACTIVE");
        return;
      }
      if (gSession.received != gSession.expected) {
        abortTransfer("INCOMPLETE", BleImageTransfer::EventType::Error);
        return;
      }
      completeTransfer();
    } else if (value.equals("ABORT")) {
      if (!gSession.active) {
        sendStatus("OK:IDLE");
      } else {
        abortTransfer("REMOTE", BleImageTransfer::EventType::Aborted);
      }
    } else if (value.equals("PING")) {
      sendStatus("OK:PONG");
    } else {
      sendStatus("ERR:UNKNOWN");
    }
  }
};

class ChunkCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    size_t len = characteristic->getLength();
    uint8_t* data = characteristic->getData();
    if (!data || len == 0) return;

    if (!gSession.active) {
      sendStatus("ERR:NOACTIVE");
      return;
    }
    if (!gSession.file) {
      abortTransfer("NOFILE", BleImageTransfer::EventType::Error);
      return;
    }

    size_t remaining = (gSession.expected >= gSession.received) ? gSession.expected - gSession.received : 0;
    if (len > remaining) {
      abortTransfer("OVERFLOW", BleImageTransfer::EventType::Error);
      return;
    }

    size_t written = gSession.file.write(reinterpret_cast<const uint8_t*>(data), len);
    if (written != len) {
      abortTransfer("WRITE", BleImageTransfer::EventType::Error);
      return;
    }

    gSession.received += len;
    gSession.lastActivity = millis();

    if (gSession.expected > 0) {
      size_t notifyStep = std::max<size_t>(gSession.expected / 10, 4096);
      if (gSession.received - gSession.lastNotified >= notifyStep || gSession.received == gSession.expected) {
        gSession.lastNotified = gSession.received;
        char status[64];
        std::snprintf(status, sizeof(status), "OK:PROG:%lu:%lu",
                      static_cast<unsigned long>(gSession.received),
                      static_cast<unsigned long>(gSession.expected));
        sendStatus(status);
      }
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    Serial.println("[BLE] central connected");
  }

  void onDisconnect(BLEServer* server) override {
    Serial.println("[BLE] central disconnected");
    if (gSession.active) {
      abortTransfer("DISCONNECT", BleImageTransfer::EventType::Aborted);
    }
    BLEDevice::startAdvertising();
  }
};

ControlCallbacks gControlCallbacks;
ChunkCallbacks gChunkCallbacks;
ServerCallbacks gServerCallbacks;

bool gStarted = false;

} // namespace

namespace BleImageTransfer {

void begin() {
  if (gStarted) return;
  ensureQueue();

  BLEDevice::init("Brosche");
  BLEDevice::setPower(ESP_PWR_LVL_P3);
  BLEDevice::setMTU(180);

  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(&gServerCallbacks);

  BLEService* service = server->createService(kServiceUuid);
  gChunkChar = service->createCharacteristic(
      kChunkCharUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  gChunkChar->setCallbacks(&gChunkCallbacks);

  gControlChar = service->createCharacteristic(
      kControlCharUuid,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_NOTIFY
  );
  gControlChar->setCallbacks(&gControlCallbacks);
  BLE2902* cccd = new BLE2902();
  cccd->setNotifications(true);
  gControlChar->addDescriptor(cccd);

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLEUUID(kServiceUuid));
  advertising->setScanResponse(true);
  advertising->start();

  gStarted = true;
  Serial.println("[BLE] Image transfer service ready");
}

void tick() {
  if (!gSession.active) return;
  uint32_t now = millis();
  if (now - gSession.lastActivity > kTransferTimeoutMs) {
    abortTransfer("TIMEOUT", EventType::Aborted);
  }
}

bool pollEvent(Event* outEvent) {
  if (!outEvent) return false;
  ensureQueue();
  if (!gEventQueue) return false;
  return xQueueReceive(gEventQueue, outEvent, 0) == pdTRUE;
}

bool isTransferActive() {
  return gSession.active;
}

size_t bytesExpected() {
  return gSession.expected;
}

size_t bytesReceived() {
  return gSession.received;
}

void setTransferEnabled(bool enabled) {
  if (enabled == gTransfersEnabled) return;
  gTransfersEnabled = enabled;
  if (!enabled && gSession.active) {
    abortTransfer("DISABLED", EventType::Aborted);
  }
}

bool transferEnabled() {
  return gTransfersEnabled;
}

} // namespace BleImageTransfer
