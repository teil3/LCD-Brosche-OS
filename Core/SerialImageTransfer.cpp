#include "SerialImageTransfer.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

#include <FS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Storage.h"

namespace SerialTransferInternal {

constexpr size_t   kMaxImageSize      = 320 * 1024;   // 320 KB Sicherheitslimit
constexpr uint32_t kTransferTimeoutMs = 15000;        // 15s Inaktivität -> Abbruch
constexpr size_t   kChunkBufferSize   = 1024;         // Puffer für eingehende Blöcke
constexpr size_t   kFilenameCapacity  = sizeof(SerialImageTransfer::Event::filename);
constexpr size_t   kLineBufferSize    = 512;
constexpr size_t   kProgressNotifyMin = 1024;         // mind. alle 1 KB Fortschritt melden

enum class RxState : uint8_t { Idle = 0, Receiving, AwaitEnd };

struct TransferSession {
  RxState state = RxState::Idle;
  File file;
  size_t expected = 0;
  size_t received = 0;
  size_t lastNotified = 0;
  uint32_t startedAt = 0;
  uint32_t lastActivity = 0;
  bool endHintSent = false;
  char filename[kFilenameCapacity];
};

QueueHandle_t gEventQueue = nullptr;
TransferSession gSession;
bool gTransfersEnabled = false;
char gLineBuffer[kLineBufferSize];
size_t gLineLength = 0;

constexpr size_t kLogLineCount = 32;
constexpr size_t kLogLineLength = 96;
char gLogBuffer[kLogLineCount][kLogLineLength];
size_t gLogHead = 0;

void ensureQueue() {
  if (!gEventQueue) {
    gEventQueue = xQueueCreate(8, sizeof(SerialImageTransfer::Event));
  }
}

void postEvent(SerialImageTransfer::EventType type,
               const char* filename,
               size_t size,
               const char* message) {
  ensureQueue();
  if (!gEventQueue) return;

  SerialImageTransfer::Event evt{};
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

void sendFormatted(const char* prefix, const char* code, const char* fmt, va_list args) {
  Serial.print(prefix);
  Serial.print(' ');
  if (code && code[0]) {
    Serial.print(code);
  }
  if (fmt && fmt[0]) {
    Serial.print(' ');
    char buffer[120];
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    Serial.print(buffer);
  }
  Serial.println();
}

void sendOk(const char* code, const char* fmt = nullptr, ...) {
  va_list args;
  va_start(args, fmt);
  sendFormatted("USB OK", code, fmt, args);
  va_end(args);
}

void sendErr(const char* code, const char* fmt = nullptr, ...) {
  va_list args;
  va_start(args, fmt);
  sendFormatted("USB ERR", code, fmt, args);
  va_end(args);
}

void logLine(const char* tag, const char* fmt = nullptr, ...) {
  char line[kLogLineLength];
  size_t pos = 0;
  if (tag && tag[0]) {
    pos = std::snprintf(line, sizeof(line), "%s", tag);
  }
  if (fmt && fmt[0]) {
    if (pos < sizeof(line) - 2) {
      line[pos++] = ' ';
    }
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line + pos, sizeof(line) - pos, fmt, args);
    va_end(args);
  }
  line[sizeof(line) - 1] = '\0';

  std::snprintf(gLogBuffer[gLogHead], sizeof gLogBuffer[gLogHead], "%s", line);
  gLogHead = (gLogHead + 1) % kLogLineCount;

  Serial.print("USB LOG ");
  Serial.println(line);
}

void cleanupFileOnError() {
  if (!gSession.filename[0]) return;
  String path = String(kFlashSlidesDir) + "/" + gSession.filename;
  if (!LittleFS.exists(path)) {
    return;
  }
  // Datei nur löschen, wenn sie aktuell nicht geöffnet ist. Falls open() scheitert (z. B. weil
  // gerade eine Slideshow sie verwendet), lassen wir den unlink aus und generieren später einen
  // neuen Dateinamen.
  File test = LittleFS.open(path.c_str(), FILE_READ);
  if (test) {
    test.close();
    LittleFS.remove(path);
  }
}

void resetSession() {
  if (gSession.file) {
    gSession.file.close();
  }
  gSession.file = File();
  gSession.state = RxState::Idle;
  gSession.expected = 0;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = 0;
  gSession.lastActivity = 0;
  gSession.endHintSent = false;
  std::memset(gSession.filename, 0, sizeof(gSession.filename));
  for (auto &line : gLogBuffer) {
    line[0] = '\0';
  }
}

bool validateSize(size_t sz) {
  return sz > 0 && sz <= kMaxImageSize;
}

void sanitizeFilename(const char* requested, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  const char* kDefault = "usb_image.jpg";

  size_t pos = 0;
  if (requested) {
    for (const char* p = requested; *p && pos < outLen - 1; ++p) {
      char ch = *p;
      if (ch >= 'A' && ch <= 'Z') {
        out[pos++] = ch + 32;
      } else if ((ch >= 'a' && ch <= 'z') ||
                 (ch >= '0' && ch <= '9') ||
                 ch == '-' || ch == '_' || ch == '.') {
        out[pos++] = ch;
      } else if (ch == ' ') {
        out[pos++] = '_';
      }
    }
  }
  out[pos] = '\0';

  if (pos == 0) {
    std::snprintf(out, outLen, "%s", kDefault);
    pos = std::strlen(out);
  }

  if (out[0] == '.') {
    std::memmove(out, out + 1, pos);
    if (pos > 0) --pos;
    out[pos] = '\0';
  }

  const char* ext = ".jpg";
  const char* dot = std::strrchr(out, '.');
  if (!dot) {
    size_t remaining = outLen - 1 - pos;
    if (remaining >= std::strlen(ext)) {
      std::strcat(out, ext);
    } else if (remaining > 0) {
      std::strncat(out, ext, remaining);
      out[outLen - 1] = '\0';
    }
  }
}

void generateUniqueFilename(char* out, size_t outLen) {
  uint32_t now = millis();
  std::snprintf(out, outLen, "usb_%08lu.jpg", static_cast<unsigned long>(now));

  String path = String(kFlashSlidesDir) + "/" + out;
  uint16_t attempt = 1;
  while (LittleFS.exists(path) && attempt < 1000) {
    std::snprintf(out, outLen, "usb_%08lu_%u.jpg",
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

bool beginTransfer(size_t size, const char* requestedName) {
  if (!gTransfersEnabled) {
    sendErr("DISABLED", "Modus nicht aktiv");
    postEvent(SerialImageTransfer::EventType::Error, "", size, "USB-Modus nicht aktiv");
    return false;
  }
  if (!ensureFlashSlidesDir()) {
    sendErr("FLASHDIR", "LittleFS Fehler");
    postEvent(SerialImageTransfer::EventType::Error, "", size, "LittleFS Fehler");
    return false;
  }
  if (!validateSize(size)) {
    sendErr("SIZE", "Ungültige Größe");
    postEvent(SerialImageTransfer::EventType::Error, "", size, "Ungültige Dateigröße");
    return false;
  }

  if (gSession.state != RxState::Idle) {
    cleanupFileOnError();
    resetSession();
  }

  char filename[kFilenameCapacity];
  filename[0] = '\0';
  sanitizeFilename(requestedName, filename, sizeof(filename));
  if (!filename[0]) {
    generateUniqueFilename(filename, sizeof(filename));
  }
  ensureUniqueOnFs(filename, sizeof(filename));

  String path = String(kFlashSlidesDir) + "/" + filename;
  gSession.file = LittleFS.open(path.c_str(), FILE_WRITE);
  if (!gSession.file) {
    sendErr("OPEN", "Datei konnte nicht angelegt werden");
    postEvent(SerialImageTransfer::EventType::Error, filename, size, "Datei konnte nicht angelegt werden");
    return false;
  }

  gSession.state = RxState::Receiving;
  gSession.expected = size;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = millis();
  gSession.lastActivity = gSession.startedAt;
  gSession.endHintSent = false;
  std::snprintf(gSession.filename, sizeof(gSession.filename), "%s", filename);

  logLine("START", "%s %lu", gSession.filename, static_cast<unsigned long>(size));

  sendOk("START", "%s %lu", gSession.filename, static_cast<unsigned long>(size));

  char msg[96];
  std::snprintf(msg, sizeof(msg), "USB Empfang: %s (%lu B)",
                gSession.filename,
                static_cast<unsigned long>(size));
  postEvent(SerialImageTransfer::EventType::Started, gSession.filename, size, msg);

  Serial.printf("[USB] START %s (%lu bytes)\n", gSession.filename, static_cast<unsigned long>(size));
  return true;
}

void abortTransfer(const char* reason, SerialImageTransfer::EventType evtType) {
  if (gSession.state == RxState::Idle) return;

  if (gSession.file) {
    gSession.file.close();
    gSession.file = File();
  }

  cleanupFileOnError();
  char fname[sizeof(gSession.filename)];
  std::snprintf(fname, sizeof(fname), "%s", gSession.filename);
  size_t received = gSession.received;

  resetSession();

  sendErr(reason ? reason : "ABORT");
  logLine("ABORT", "%s %lu", reason ? reason : "?", static_cast<unsigned long>(received));
  postEvent(evtType, fname, received, reason ? reason : "");
  Serial.printf("[USB] ABORT (%s) after %lu bytes\n",
                reason ? reason : "no-reason",
                static_cast<unsigned long>(received));
}

void completeTransfer() {
  if (gSession.state != RxState::AwaitEnd) {
    sendErr("NOACTIVE", "Kein aktiver Transfer");
    return;
  }

  char fname[sizeof(gSession.filename)];
  std::snprintf(fname, sizeof(fname), "%s", gSession.filename);
  size_t received = gSession.received;

  gSession.file.close();
  gSession.file = File();
  gSession.state = RxState::Idle;
  gSession.expected = 0;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = 0;
  gSession.lastActivity = 0;
  gSession.endHintSent = false;
  std::memset(gSession.filename, 0, sizeof(gSession.filename));

  sendOk("END", "%s %lu", fname, static_cast<unsigned long>(received));
  postEvent(SerialImageTransfer::EventType::Completed, fname, received, "USB Übertragung abgeschlossen");
  Serial.printf("[USB] COMPLETE %s (%lu bytes)\n",
                fname,
                static_cast<unsigned long>(received));
}

void processData() {
  if (gSession.state != RxState::Receiving) return;
  if (!gSession.file) {
    abortTransfer("NOFILE", SerialImageTransfer::EventType::Error);
    return;
  }

  size_t remaining = (gSession.expected > gSession.received)
                     ? (gSession.expected - gSession.received)
                     : 0;
  if (remaining == 0) {
    gSession.state = RxState::AwaitEnd;
    return;
  }

  int available = Serial.available();
  if (available <= 0) {
    return;
  }
  size_t toRead = std::min<size_t>(remaining, static_cast<size_t>(available));

  uint8_t buffer[1024];
  while (toRead > 0) {
    size_t chunk = std::min<size_t>(sizeof(buffer), toRead);
    size_t readCount = Serial.readBytes(buffer, chunk);
    if (readCount == 0) {
      break;
    }
    size_t written = gSession.file.write(buffer, readCount);
    if (written != readCount) {
      abortTransfer("WRITE", SerialImageTransfer::EventType::Error);
      return;
    }
    gSession.received += written;
    gSession.lastActivity = millis();
    toRead -= written;

    logLine("READ", "%lu/%lu chunk=%u avail=%u",
            static_cast<unsigned long>(gSession.received),
            static_cast<unsigned long>(gSession.expected),
            static_cast<unsigned>(written),
            static_cast<unsigned>(Serial.available()));
  }

  if (gSession.expected > 0 && gSession.received <= gSession.expected) {
    size_t notifyStep = std::max<size_t>(gSession.expected / 16, kProgressNotifyMin);
    if (gSession.received - gSession.lastNotified >= notifyStep ||
        gSession.received == gSession.expected) {
      gSession.lastNotified = gSession.received;
      sendOk("PROG", "%lu %lu",
             static_cast<unsigned long>(gSession.received),
             static_cast<unsigned long>(gSession.expected));
      logLine("PROG", "%lu %lu",
              static_cast<unsigned long>(gSession.received),
              static_cast<unsigned long>(gSession.expected));
    }
  }

  if (gSession.received >= gSession.expected) {
    gSession.state = RxState::AwaitEnd;
    if (!gSession.endHintSent) {
      sendOk("MSG", "Daten empfangen, bitte END senden");
      gSession.endHintSent = true;
    }
  }
}

void processLine(const char* line) {
  if (!line) return;
  if (!line[0]) return;

  if (gSession.state == RxState::Receiving) {
    // Ignoriere Kommandos während des Empfangs
    return;
  }

  if (strncmp(line, "START", 5) == 0) {
    const char* ptr = line + 5;
    while (*ptr == ' ') ++ptr;
    if (!*ptr) {
      sendErr("STARTFMT", "START <size> [name]");
      return;
    }
    char* endPtr = nullptr;
    unsigned long sz = std::strtoul(ptr, &endPtr, 10);
    if (ptr == endPtr) {
      sendErr("STARTFMT", "Ungültige Größe");
      return;
    }
    while (endPtr && *endPtr == ' ') ++endPtr;
    const char* name = (endPtr && *endPtr) ? endPtr : nullptr;
    beginTransfer(static_cast<size_t>(sz), name);
    return;
  }

  if (std::strcmp(line, "END") == 0) {
    if (gSession.state != RxState::AwaitEnd) {
      sendErr("NOACTIVE", "Kein aktiver Transfer");
      return;
    }
    if (gSession.received != gSession.expected) {
      abortTransfer("INCOMPLETE", SerialImageTransfer::EventType::Error);
      return;
    }
    completeTransfer();
    return;
  }

  if (std::strcmp(line, "ABORT") == 0) {
    if (gSession.state == RxState::Idle) {
      sendOk("IDLE");
    } else {
      abortTransfer("REMOTE", SerialImageTransfer::EventType::Aborted);
    }
    return;
  }

  if (std::strcmp(line, "PING") == 0) {
    sendOk("PONG");
    return;
  }

  if (std::strcmp(line, "LOG") == 0 || std::strcmp(line, "DUMPLOG") == 0) {
    sendOk("LOG", "%u", static_cast<unsigned>(kLogLineCount));
    Serial.println("USB LOG STATUS DISABLED");
    Serial.printf("USB LOG STATUS STATE=%u ACTIVE=%u ENABLED=%u\n",
                  static_cast<unsigned>(gSession.state),
                  SerialImageTransfer::isTransferActive(),
                  gTransfersEnabled);
    size_t idx = gLogHead;
    for (size_t i = 0; i < kLogLineCount; ++i) {
      const char* entry = gLogBuffer[idx];
      if (entry[0]) {
        Serial.print("USB LOG ");
        Serial.println(entry);
      }
      idx = (idx + 1) % kLogLineCount;
    }
    return;
  }

  if (!line[0] || std::strlen(line) <= 2 || line[0] == '[' || line[0] == '<' || line[0] == '!' || line[0] == '=') {
    Serial.printf("[USB] IGN %s\n", line);
    return;
  }

  Serial.printf("[USB] UNKNOWN CMD %s\n", line);
  // Kein sendErr, damit Browser-Transfers nicht abgebrochen werden.
}

void processIncoming() {
  if (gSession.state == RxState::Receiving) {
    processData();
    return;
  }

  if (!gTransfersEnabled) {
    while (Serial.available() > 0) {
      int byteVal = Serial.read();
      if (byteVal < 0) break;
      char c = static_cast<char>(byteVal);
      if (c == '\n') {
        Serial.println("USB LOG IGNORE (disabled)\n");
        break;
      }
    }
    return;
  }

  while (Serial.available() > 0) {
    int byteVal = Serial.read();
    if (byteVal < 0) break;
    char c = static_cast<char>(byteVal);
    if (c == '\r') continue;
    if (c == '\n') {
      gLineBuffer[gLineLength] = '\0';
      processLine(gLineBuffer);
      gLineLength = 0;
      continue;
    }
    if (gLineLength + 1 >= kLineBufferSize) {
      // Zeile zu lang -> Reset und Fehler
      gLineLength = 0;
      sendErr("LINE", "Zeile zu lang");
      continue;
    }
    gLineBuffer[gLineLength++] = c;
  }
}

} // namespace SerialTransferInternal

namespace SerialImageTransfer {

void begin() {
  Serial.setRxBufferSize(4096);
  SerialTransferInternal::ensureQueue();
  SerialTransferInternal::gLineLength = 0;
  SerialTransferInternal::resetSession();
  SerialTransferInternal::gLogHead = 0;
}

void tick() {
  if (!SerialTransferInternal::gTransfersEnabled &&
      SerialTransferInternal::gSession.state != SerialTransferInternal::RxState::Idle) {
    SerialTransferInternal::abortTransfer("DISABLED", EventType::Aborted);
    return;
  }

  if (SerialTransferInternal::gSession.state == SerialTransferInternal::RxState::Receiving) {
    SerialTransferInternal::processData();
  }
  if (SerialTransferInternal::gSession.state != SerialTransferInternal::RxState::Receiving) {
    SerialTransferInternal::processIncoming();
  }

  if (SerialTransferInternal::gSession.state != SerialTransferInternal::RxState::Idle) {
    uint32_t now = millis();
    if (now - SerialTransferInternal::gSession.lastActivity > SerialTransferInternal::kTransferTimeoutMs) {
      SerialTransferInternal::abortTransfer("TIMEOUT", EventType::Aborted);
    }
  }
}

bool pollEvent(Event* outEvent) {
  if (!outEvent) return false;
  SerialTransferInternal::ensureQueue();
  if (!SerialTransferInternal::gEventQueue) return false;
  return xQueueReceive(SerialTransferInternal::gEventQueue, outEvent, 0) == pdTRUE;
}

bool isTransferActive() {
  return SerialTransferInternal::gSession.state == SerialTransferInternal::RxState::Receiving ||
         SerialTransferInternal::gSession.state == SerialTransferInternal::RxState::AwaitEnd;
}

size_t bytesExpected() {
  return SerialTransferInternal::gSession.expected;
}

size_t bytesReceived() {
  return SerialTransferInternal::gSession.received;
}

void setTransferEnabled(bool enabled) {
  if (enabled == SerialTransferInternal::gTransfersEnabled) return;
  SerialTransferInternal::gTransfersEnabled = enabled;
  if (!enabled && SerialTransferInternal::gSession.state != SerialTransferInternal::RxState::Idle) {
    SerialTransferInternal::abortTransfer("DISABLED", EventType::Aborted);
  }
  if (enabled) {
    SerialTransferInternal::sendOk("READY", "USB-Modus aktiv");
  }
}

bool transferEnabled() {
  return SerialTransferInternal::gTransfersEnabled;
}

} // namespace SerialImageTransfer
