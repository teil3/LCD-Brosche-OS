#pragma once

#ifndef CORE_I18N_H
#define CORE_I18N_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>

/**
 * Internationalization (i18n) support for multi-language UI.
 *
 * Loads translations from /system/i18n.json in LittleFS and provides
 * fast lookup with RAM caching of the active language.
 *
 * Supported languages are defined in the JSON file.
 * Language preference is stored persistently in NVS.
 */
class I18n {
public:
  /**
   * Initialize i18n system.
   * Loads language preference from NVS and parses i18n.json.
   * Must be called after LittleFS.begin().
   */
  void begin();

  /**
   * Translate a key to the current language.
   *
   * @param key Hierarchical key like "slideshow.no_images"
   * @return Translated string, or the key itself if not found (fallback)
   */
  const char* t(const char* key);

  /**
   * Translate with one parameter substitution.
   * Replaces {0} in the translated string.
   *
   * Example: t("system.copying", "5") where JSON has "Kopiere {0}"
   * Returns: "Kopiere 5"
   */
  String t(const char* key, const String& p0);

  /**
   * Translate with two parameter substitutions.
   * Replaces {0} and {1} in the translated string.
   */
  String t(const char* key, const String& p0, const String& p1);

  /**
   * Translate with three parameter substitutions.
   * Replaces {0}, {1}, and {2} in the translated string.
   */
  String t(const char* key, const String& p0, const String& p1, const String& p2);

  /**
   * Change the active language.
   *
   * @param lang Language code ("de", "en", "fr", "it")
   * @return true if language was changed successfully
   */
  bool setLanguage(const char* lang);

  /**
   * Get current language code.
   *
   * @return Language code like "de", "en", etc.
   */
  const char* currentLanguage() const;

  /**
   * Get current language index.
   *
   * @return 0 for first language, 1 for second, etc.
   */
  uint8_t currentLanguageIndex() const { return langIndex_; }

  /**
   * Get list of available languages from JSON.
   *
   * @return Array of language codes
   */
  const char* const* availableLanguages() const { return availableLangs_; }

  /**
   * Get number of available languages.
   */
  uint8_t languageCount() const { return langCount_; }

  /**
   * Check if i18n is properly initialized.
   */
  bool isReady() const { return ready_; }

private:
  static constexpr size_t kMaxLanguages = 4;  // DE, EN, FR, IT
  static constexpr size_t kJsonBufferSize = 12288;  // 12 KB for JSON document
  static constexpr const char* kI18nFilePath = "/system/i18n.json";
  static constexpr const char* kPrefsNamespace = "i18n";
  static constexpr const char* kPrefsKeyLang = "lang";

  JsonDocument doc_;
  uint8_t langIndex_ = 0;
  bool ready_ = false;
  const char* availableLangs_[kMaxLanguages] = {nullptr};
  uint8_t langCount_ = 0;
  char currentLang_[3] = "de";  // 2-letter code + null terminator

  bool loadFromFile_();
  void loadAvailableLanguages_();
  void saveLanguagePreference_();
  String loadLanguagePreference_();
  JsonVariant lookupKey_(const char* key);
};

// Global i18n instance
extern I18n i18n;

#endif // CORE_I18N_H
