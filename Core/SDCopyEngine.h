#ifndef SDCOPYENGINE_H
#define SDCOPYENGINE_H

#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SD.h>
#include <LittleFS.h>

/**
 * SDCopyEngine - Standalone SD-to-Flash file copy engine
 *
 * Scans SD card for supported files (images, fonts, configs, lua scripts)
 * and copies them to LittleFS in an incremental, non-blocking manner.
 *
 * Used by both SystemUI (interactive copy with UI) and boot auto-setup.
 */
class SDCopyEngine {
 public:
  enum class State : uint8_t {
    Idle = 0,
    Running,
    Finished
  };

  enum class Outcome : uint8_t {
    None = 0,
    Success,
    Error,
    Aborted
  };

  enum class FileType : uint8_t {
    Bootlogo = 0,
    Config,
    Font,
    Lua,
    Jpg,
    Gif
  };

  struct CopyItem {
    String path;      // Source path on SD card
    String name;      // Filename for display
    size_t size = 0;  // File size in bytes
    FileType type = FileType::Jpg;
    String destPath;  // Destination path in LittleFS
  };

  SDCopyEngine();
  ~SDCopyEngine();

  /**
   * Scan SD card and build copy queue.
   * Returns true if files were found, false otherwise.
   */
  bool prepare();

  /**
   * Process one chunk of copying.
   * Call repeatedly until getState() != Running.
   * Budget: ~20ms per call to keep system responsive.
   */
  void tick();

  /**
   * Request abort. Next tick() will finalize with Aborted outcome.
   */
  void abort();

  /**
   * Reset to idle state, clearing all state and closing files.
   */
  void reset();

  // State queries
  State getState() const { return state_; }
  Outcome getOutcome() const { return outcome_; }
  String getErrorMessage() const { return outcomeMessage_; }

  // Progress info
  size_t getFileCount() const { return queue_.size(); }
  size_t getCurrentFileIndex() const { return queueIndex_; }
  String getCurrentFileName() const;
  size_t getTotalBytes() const { return bytesTotal_; }
  size_t getBytesDone() const { return bytesDone_; }
  int getPercentDone() const;

 private:
  static constexpr size_t kBufferSize = 2048;
  static constexpr uint32_t kTickBudgetMs = 20;

  bool ensureSdReady_();
  bool ensureFlashReady_();
  void closeFiles_();
  void finalize_(Outcome outcome, const String& message);

  State state_ = State::Idle;
  Outcome outcome_ = Outcome::None;
  String outcomeMessage_;
  bool abortRequest_ = false;

  std::vector<CopyItem> queue_;
  size_t queueIndex_ = 0;
  size_t bytesTotal_ = 0;
  size_t bytesDone_ = 0;
  size_t fileBytesDone_ = 0;

  File srcFile_;
  File dstFile_;
  uint8_t* buffer_ = nullptr;
};

#endif // SDCOPYENGINE_H
