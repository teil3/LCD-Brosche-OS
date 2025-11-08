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
constexpr size_t   kLineBufferSize    = 160;

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
  bool progressPending = false;
  uint8_t* ramBuffer = nullptr;
  size_t ramCapacity = 0;
  char filename[kFilenameCapacity];
  char targetDir[kFilenameCapacity];
};

QueueHandle_t gEventQueue = nullptr;
TransferSession gSession;
bool gTransfersEnabled = false;
char gLineBuffer[kLineBufferSize];
size_t gLineLength = 0;

void sendOk(const char* code, const char* fmt, ...);
void sendErr(const char* code, const char* fmt, ...);

bool isProtectedPath(const String& path) {
  if (path.isEmpty()) return true;
  String lower = path;
  lower.toLowerCase();

  if (lower == "/textapp.cfg") {
    return true;
  }

  if (lower == "/system/fonts" || lower.startsWith("/system/fonts/")) {
    return true;
  }

  if (lower == "/system/bootlogo.jpg") {
    return true;
  }

  return false;
}

String normalizePath(const char* raw) {
  if (!raw) return String();
  String path = String(raw);
  path.trim();
  if (path.isEmpty()) return path;
  if (!path.startsWith("/")) {
    path = "/" + path;
  }

  // Collapse duplicate slashes (simple pass)
  String normalized;
  normalized.reserve(path.length());
  bool lastWasSlash = false;
  for (size_t i = 0; i < path.length(); ++i) {
    char ch = path[i];
    if (ch == '/') {
      if (!lastWasSlash || normalized.length() == 0) {
        normalized += ch;
      }
      lastWasSlash = true;
    } else {
      normalized += ch;
      lastWasSlash = false;
    }
  }
  // Remove trailing slash except root
  while (normalized.length() > 1 && normalized.endsWith("/")) {
    normalized.remove(normalized.length() - 1);
  }
  return normalized;
}

void deleteFileAtPath(const char* rawPath) {
  String path = normalizePath(rawPath);
  if (path.isEmpty()) {
    sendErr("DELPATH", "Pfad fehlt");
    return;
  }

  if (!LittleFS.exists(path)) {
    sendErr("DELNOENT", "%s", path.c_str());
    return;
  }

  File entry = LittleFS.open(path, FILE_READ);
  if (!entry) {
    sendErr("DELOPEN", "Pfad unlesbar");
    return;
  }

  bool isDir = entry.isDirectory();
  entry.close();

  if (isDir) {
    sendErr("DELDIR", "Nur Dateien löschbar");
    return;
  }

  if (isProtectedPath(path)) {
    sendErr("DELPROT", "Datei geschützt");
    return;
  }

  if (!LittleFS.remove(path)) {
    sendErr("DELFAIL", "%s", path.c_str());
    return;
  }

  sendOk("DELETE", "%s", path.c_str());
}

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

void cleanupFileOnError() {
  if (!gSession.filename[0]) return;
  const char* dir = gSession.targetDir[0] ? gSession.targetDir : kFlashSlidesDir;
  String path = String(dir) + "/" + gSession.filename;
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

void listDirectory(const char* dirArg) {
  String dir = (dirArg && dirArg[0]) ? String(dirArg) : String("/");
  dir.trim();
  if (dir.isEmpty()) dir = "/";
  if (!dir.startsWith("/")) {
    dir = "/" + dir;
  }

  // Normalise trailing slash (except root)
  while (dir.length() > 1 && dir.endsWith("/")) {
    dir.remove(dir.length() - 1);
  }

  File root = LittleFS.open(dir.c_str(), FILE_READ);
  if (!root) {
    sendErr("LISTOPEN", "%s", dir.c_str());
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    sendErr("LISTTYPE", "%s", dir.c_str());
    return;
  }

  size_t count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    String name = entry.name();
    if (name.startsWith(dir)) {
      name = name.substring(dir.length());
    }
    if (name.startsWith("/")) {
      name.remove(0, 1);
    }
    if (name.isEmpty()) {
      name = entry.name();
    }

    bool isDir = entry.isDirectory();
    unsigned long size = isDir ? 0UL : static_cast<unsigned long>(entry.size());
    sendOk("LIST", "%c %s %lu", isDir ? 'D' : 'F', name.c_str(), size);
    entry.close();
    ++count;
    delay(0);
  }

  root.close();
  sendOk("LISTDONE", "%lu", static_cast<unsigned long>(count));
}

void sendFsInfo() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t available = (total >= used) ? (total - used) : 0;
  sendOk("FSINFO", "%lu %lu %lu",
         static_cast<unsigned long>(total),
         static_cast<unsigned long>(used),
         static_cast<unsigned long>(available));
}

void sendFile(const char* path) {
  if (!path || !path[0]) {
    sendErr("READPATH", "Kein Pfad angegeben");
    return;
  }

  String filePath = String(path);
  filePath.trim();

  // Ensure path starts with /
  if (!filePath.startsWith("/")) {
    filePath = "/" + filePath;
  }

  if (!LittleFS.exists(filePath.c_str())) {
    sendErr("READNOENT", "Datei nicht gefunden: %s", filePath.c_str());
    return;
  }

  File file = LittleFS.open(filePath.c_str(), FILE_READ);
  if (!file) {
    sendErr("READOPEN", "Datei konnte nicht geoeffnet werden");
    return;
  }

  if (file.isDirectory()) {
    file.close();
    sendErr("READDIR", "Pfad ist ein Verzeichnis");
    return;
  }

  size_t fileSize = file.size();

  // Extract filename from path
  String filename = filePath;
  int lastSlash = filename.lastIndexOf('/');
  if (lastSlash >= 0) {
    filename = filename.substring(lastSlash + 1);
  }

  // Send header
  sendOk("READ", "%lu %s", static_cast<unsigned long>(fileSize), filename.c_str());

  #ifdef USB_DEBUG
    Serial.printf("[USB] READ %s (%lu bytes)\n", filePath.c_str(), static_cast<unsigned long>(fileSize));
  #endif

  // Send file content as raw bytes
  uint8_t buffer[512];
  size_t totalSent = 0;

  while (file.available()) {
    size_t bytesRead = file.readBytes((char*)buffer, sizeof(buffer));
    if (bytesRead == 0) break;

    Serial.write(buffer, bytesRead);
    totalSent += bytesRead;
    delay(0); // Yield to prevent watchdog
  }

  file.close();

  // Send end marker
  sendOk("READEND", "%lu", static_cast<unsigned long>(totalSent));

  #ifdef USB_DEBUG
    Serial.printf("[USB] READEND %lu bytes\n", static_cast<unsigned long>(totalSent));
  #endif
}

void resetSession() {
  if (gSession.file) {
    gSession.file.close();
  }
  if (gSession.ramBuffer) {
    free(gSession.ramBuffer);
    gSession.ramBuffer = nullptr;
    gSession.ramCapacity = 0;
  }
  gSession.file = File();
  gSession.state = RxState::Idle;
  gSession.expected = 0;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = 0;
  gSession.lastActivity = 0;
  gSession.endHintSent = false;
  gSession.progressPending = false;
  std::memset(gSession.filename, 0, sizeof(gSession.filename));
  std::memset(gSession.targetDir, 0, sizeof(gSession.targetDir));
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

void generateUniqueFilename(char* out, size_t outLen, const char* dir) {
  uint32_t now = millis();
  std::snprintf(out, outLen, "usb_%08lu.jpg", static_cast<unsigned long>(now));

  String path = String(dir) + "/" + out;
  uint16_t attempt = 1;
  while (LittleFS.exists(path) && attempt < 1000) {
    std::snprintf(out, outLen, "usb_%08lu_%u.jpg",
                  static_cast<unsigned long>(now), attempt);
    path = String(dir) + "/" + out;
    ++attempt;
  }
}

void ensureUniqueOnFs(char* nameBuf, size_t bufLen, const char* dir) {
  if (!nameBuf || !nameBuf[0]) {
    generateUniqueFilename(nameBuf, bufLen, dir);
    return;
  }

  // Only generate unique names for /slides directory
  // For all other directories (/, /system, etc.), overwrite existing files
  if (dir && std::strcmp(dir, kFlashSlidesDir) != 0) {
    return;  // Use the filename as-is, will overwrite existing file
  }

  // For /slides directory, make filename unique
  String base(nameBuf);
  String ext(".jpg");
  int dotIdx = base.lastIndexOf('.');
  if (dotIdx >= 0) {
    ext = base.substring(dotIdx);
    base = base.substring(0, dotIdx);
  }

  String path = String(dir) + "/" + String(nameBuf);
  if (!LittleFS.exists(path)) {
    return;
  }

  for (int i = 1; i < 1000; ++i) {
    String candidate = base + "-" + String(i) + ext;
    if (candidate.length() >= static_cast<int>(bufLen)) {
      continue;
    }
    path = String(dir) + "/" + candidate;
    if (!LittleFS.exists(path)) {
      std::snprintf(nameBuf, bufLen, "%s", candidate.c_str());
      return;
    }
  }

  generateUniqueFilename(nameBuf, bufLen, dir);
}

bool beginTransfer(size_t size, const char* requestedName, const char* targetDir = nullptr) {
  if (!gTransfersEnabled) {
    sendErr("DISABLED", "Modus nicht aktiv");
    postEvent(SerialImageTransfer::EventType::Error, "", size, "USB-Modus nicht aktiv");
    return false;
  }

  // Use provided directory or default to /slides
  const char* dir = (targetDir && targetDir[0]) ? targetDir : kFlashSlidesDir;

  // Ensure target directory exists
  if (!ensureDirectory(dir)) {
    sendErr("DIRFAIL", "Verzeichnis konnte nicht erstellt werden");
    postEvent(SerialImageTransfer::EventType::Error, "", size, "Verzeichnis-Fehler");
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
    generateUniqueFilename(filename, sizeof(filename), dir);
  }
  ensureUniqueOnFs(filename, sizeof(filename), dir);

  if (size <= kMaxImageSize) {
    uint8_t* buffer = static_cast<uint8_t*>(malloc(size));
    if (buffer) {
      gSession.ramBuffer = buffer;
      gSession.ramCapacity = size;
    }
  }

  if (!gSession.ramBuffer) {
    String path = String(dir) + "/" + filename;
    gSession.file = LittleFS.open(path.c_str(), FILE_WRITE);
    if (!gSession.file) {
      sendErr("OPEN", "Datei konnte nicht angelegt werden");
      postEvent(SerialImageTransfer::EventType::Error, filename, size, "Datei konnte nicht angelegt werden");
      return false;
    }
  }

  gSession.state = RxState::Receiving;
  gSession.expected = size;
  gSession.received = 0;
  gSession.lastNotified = 0;
  gSession.startedAt = millis();
  gSession.lastActivity = gSession.startedAt;
  gSession.endHintSent = false;
  gSession.progressPending = false;
  std::snprintf(gSession.filename, sizeof(gSession.filename), "%s", filename);
  std::snprintf(gSession.targetDir, sizeof(gSession.targetDir), "%s", dir);

  sendOk("START", "%s %lu", gSession.filename, static_cast<unsigned long>(size));

  char msg[96];
  std::snprintf(msg, sizeof(msg), "USB Empfang: %s (%lu B)",
                gSession.filename,
                static_cast<unsigned long>(size));
  postEvent(SerialImageTransfer::EventType::Started, gSession.filename, size, msg);

  #ifdef USB_DEBUG
    Serial.printf("[USB] START %s (%lu bytes)\n", gSession.filename, static_cast<unsigned long>(size));
  #endif
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
  postEvent(evtType, fname, received, reason ? reason : "");
  #ifdef USB_DEBUG
    Serial.printf("[USB] ABORT (%s) after %lu bytes\n",
                  reason ? reason : "no-reason",
                  static_cast<unsigned long>(received));
  #endif
}

void completeTransfer() {
  if (gSession.state != RxState::AwaitEnd) {
    sendErr("NOACTIVE", "Kein aktiver Transfer");
    return;
  }

  char fname[sizeof(gSession.filename)];
  std::snprintf(fname, sizeof(fname), "%s", gSession.filename);
  const char* dir = gSession.targetDir[0] ? gSession.targetDir : kFlashSlidesDir;
  size_t received = gSession.received;
  size_t expected = gSession.expected;
  bool progressPending = gSession.progressPending;
  uint8_t* ramPtr = gSession.ramBuffer;
  bool ramMode = (ramPtr != nullptr);

  if (ramMode) {
    String path = String(dir) + "/" + fname;
    File out = LittleFS.open(path.c_str(), FILE_WRITE);
    if (!out) {
      sendErr("FLASH", "Datei konnte nicht angelegt werden");
      resetSession();
      return;
    }
    size_t offset = 0;
    while (offset < received) {
      size_t chunk = std::min<size_t>(512, received - offset);
      size_t written = out.write(ramPtr + offset, chunk);
      if (written != chunk) {
        out.close();
        LittleFS.remove(path);
        sendErr("FLASH", "Schreibfehler");
        resetSession();
        return;
      }
      offset += written;
      delay(0);
    }
    out.close();
  } else if (gSession.file) {
    gSession.file.close();
    gSession.file = File();
  }

  resetSession();

  if (progressPending) {
    sendOk("PROG", "%lu %lu",
           static_cast<unsigned long>(received),
           static_cast<unsigned long>(expected));
  }
  sendOk("END", "%s %lu", fname, static_cast<unsigned long>(received));
  postEvent(SerialImageTransfer::EventType::Completed, fname, received, "USB Übertragung abgeschlossen");
  #ifdef USB_DEBUG
    Serial.printf("[USB] COMPLETE %s (%lu bytes)\n",
                  fname,
                  static_cast<unsigned long>(received));
  #endif
}

void processData() {
  if (gSession.state != RxState::Receiving) return;
  if (!gSession.file && !gSession.ramBuffer) {
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
    if (gSession.ramBuffer) {
      if (gSession.received + readCount > gSession.ramCapacity) {
        abortTransfer("RAM", SerialImageTransfer::EventType::Error);
        return;
      }
      std::memcpy(gSession.ramBuffer + gSession.received, buffer, readCount);
      gSession.received += readCount;
      toRead -= readCount;
    } else {
      size_t written = gSession.file.write(buffer, readCount);
      if (written != readCount) {
        abortTransfer("WRITE", SerialImageTransfer::EventType::Error);
        return;
      }
      gSession.received += written;
      toRead -= written;
    }
    gSession.lastActivity = millis();
  }

  if (gSession.received >= gSession.expected) {
    gSession.state = RxState::AwaitEnd;
    gSession.endHintSent = true;
    gSession.progressPending = true;
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
      sendErr("STARTFMT", "START <size> [name] [directory]");
      return;
    }
    char* endPtr = nullptr;
    unsigned long sz = std::strtoul(ptr, &endPtr, 10);
    if (ptr == endPtr) {
      sendErr("STARTFMT", "Ungültige Größe");
      return;
    }

    // Parse filename (second argument)
    while (endPtr && *endPtr == ' ') ++endPtr;
    const char* nameStart = endPtr;
    const char* nameEnd = nameStart;
    while (*nameEnd && *nameEnd != ' ') ++nameEnd;

    char nameBuf[kFilenameCapacity];
    nameBuf[0] = '\0';
    if (nameStart && nameStart < nameEnd) {
      size_t len = std::min<size_t>(nameEnd - nameStart, sizeof(nameBuf) - 1);
      std::memcpy(nameBuf, nameStart, len);
      nameBuf[len] = '\0';
    }
    const char* name = nameBuf[0] ? nameBuf : nullptr;

    // Parse directory (third argument, optional)
    while (*nameEnd == ' ') ++nameEnd;
    const char* dir = (*nameEnd) ? nameEnd : nullptr;

    beginTransfer(static_cast<size_t>(sz), name, dir);
    return;
  }

  if (std::strncmp(line, "LIST", 4) == 0) {
    const char* ptr = line + 4;
    while (*ptr == ' ') ++ptr;
    listDirectory(ptr);
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

  if (std::strcmp(line, "FSINFO") == 0) {
    sendFsInfo();
    return;
  }

  if (std::strncmp(line, "READ", 4) == 0) {
    const char* ptr = line + 4;
    while (*ptr == ' ') ++ptr;
    if (!*ptr) {
      sendErr("READFMT", "READ <filepath>");
      return;
    }
    sendFile(ptr);
    return;
  }

  if (std::strncmp(line, "DELETE", 6) == 0) {
    const char* ptr = line + 6;
    while (*ptr == ' ') ++ptr;
    if (!*ptr) {
      sendErr("DELFMT", "DELETE <filepath>");
      return;
    }
    deleteFileAtPath(ptr);
    return;
  }

  if (!line[0] || std::strlen(line) <= 2 || line[0] == '[' || line[0] == '<' || line[0] == '!' || line[0] == '=') {
    #ifdef USB_DEBUG
      Serial.printf("[USB] IGN %s\n", line);
    #endif
    return;
  }

  #ifdef USB_DEBUG
    Serial.printf("[USB] UNKNOWN CMD %s\n", line);
  #endif
  // Kein sendErr, damit Browser-Transfers nicht abgebrochen werden.
}

void processIncoming() {
  if (gSession.state == RxState::Receiving) {
    processData();
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
