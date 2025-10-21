#include "Storage.h"

bool mountLittleFs(bool formatOnFail) {
  auto tryMount = [](bool format) {
    return LittleFS.begin(format, kLittleFsBasePath, 10, kLittleFsPartition);
  };

  if (tryMount(false)) {
    return true;
  }
  if (formatOnFail && tryMount(true)) {
    return true;
  }
  return false;
}

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
