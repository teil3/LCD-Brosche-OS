#ifndef CORE_STORAGE_H
#define CORE_STORAGE_H

#include <FS.h>
#include <SD.h>
#include <LittleFS.h>

enum class SlideSource : uint8_t {
  SDCard = 0,
  Flash  = 1,
};

constexpr const char* kFlashSlidesDir = "/slides";

inline const char* slideSourceLabel(SlideSource src) {
  switch (src) {
    case SlideSource::Flash:  return "Flash";
    case SlideSource::SDCard: return "SD";
    default:                  return "?";
  }
}

inline fs::FS* filesystemFor(SlideSource src) {
  switch (src) {
    case SlideSource::Flash:  return &LittleFS;
    case SlideSource::SDCard: return &SD;
    default:                  return nullptr;
  }
}

bool ensureFlashSlidesDir();
bool clearFlashSlidesDir();

#endif // CORE_STORAGE_H
