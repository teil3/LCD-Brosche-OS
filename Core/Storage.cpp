#include "Storage.h"

bool ensureFlashSlidesDir() {
  if (!LittleFS.exists(kFlashSlidesDir)) {
    return LittleFS.mkdir(kFlashSlidesDir);
  }
  return true;
}

bool clearFlashSlidesDir() {
  if (!LittleFS.exists(kFlashSlidesDir)) {
    return true;
  }

  File root = LittleFS.open(kFlashSlidesDir);
  if (!root || !root.isDirectory()) {
    return false;
  }

  bool ok = true;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) {
      // keine Unterordner erwartet
      continue;
    }
    String name = f.name();
    int s = name.lastIndexOf('/');
    if (s >= 0) name = name.substring(s + 1);
    const String path = String(kFlashSlidesDir) + "/" + name;
    if (!LittleFS.remove(path)) {
      ok = false;
    }
  }
  root.close();
  return ok;
}
