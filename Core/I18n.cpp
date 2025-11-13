#include "I18n.h"

// Global instance
I18n i18n;

void I18n::begin() {
  ready_ = false;
  langIndex_ = 0;
  langCount_ = 0;

  // Load JSON from LittleFS
  if (!loadFromFile_()) {
    Serial.println("[I18n] Failed to load i18n.json");
    return;
  }

  // Parse available languages from JSON
  loadAvailableLanguages_();

  if (langCount_ == 0) {
    Serial.println("[I18n] No languages found in i18n.json");
    return;
  }

  // Get default language from JSON
  const char* defaultLang = doc_["meta"]["default"] | "de";

  // Load saved language preference (or use default)
  String savedLang = loadLanguagePreference_();
  if (savedLang.isEmpty()) {
    savedLang = String(defaultLang);
  }

  // Set active language
  setLanguage(savedLang.c_str());

  ready_ = true;
  Serial.printf("[I18n] Ready. Language: %s (%d of %d)\n",
                currentLang_, langIndex_ + 1, langCount_);
}

const char* I18n::t(const char* key) {
  if (!ready_ || !key) {
    return key ? key : "";
  }

  JsonVariant entry = lookupKey_(key);

  if (entry.isNull()) {
    Serial.printf("[I18n] Missing key: %s\n", key);
    return key;  // Fallback: return the key itself
  }

  // Get the translation for current language index
  if (entry.is<JsonArray>()) {
    JsonArray arr = entry.as<JsonArray>();
    if (langIndex_ < arr.size()) {
      return arr[langIndex_] | key;
    }
  }

  return key;
}

String I18n::t(const char* key, const String& p0) {
  String result = String(t(key));
  result.replace("{0}", p0);
  return result;
}

String I18n::t(const char* key, const String& p0, const String& p1) {
  String result = String(t(key));
  result.replace("{0}", p0);
  result.replace("{1}", p1);
  return result;
}

String I18n::t(const char* key, const String& p0, const String& p1, const String& p2) {
  String result = String(t(key));
  result.replace("{0}", p0);
  result.replace("{1}", p1);
  result.replace("{2}", p2);
  return result;
}

bool I18n::setLanguage(const char* lang) {
  if (!lang || langCount_ == 0) {
    return false;
  }

  // Find language index
  for (uint8_t i = 0; i < langCount_; i++) {
    if (availableLangs_[i] && strcmp(availableLangs_[i], lang) == 0) {
      langIndex_ = i;
      strncpy(currentLang_, lang, sizeof(currentLang_) - 1);
      currentLang_[sizeof(currentLang_) - 1] = '\0';
      saveLanguagePreference_();
      Serial.printf("[I18n] Language changed to: %s (index %d)\n", lang, i);
      return true;
    }
  }

  Serial.printf("[I18n] Language not found: %s\n", lang);
  return false;
}

const char* I18n::currentLanguage() const {
  return currentLang_;
}

bool I18n::loadFromFile_() {
  if (!LittleFS.exists(kI18nFilePath)) {
    Serial.printf("[I18n] File not found: %s\n", kI18nFilePath);
    return false;
  }

  File file = LittleFS.open(kI18nFilePath, "r");
  if (!file) {
    Serial.printf("[I18n] Failed to open: %s\n", kI18nFilePath);
    return false;
  }

  DeserializationError error = deserializeJson(doc_, file);
  file.close();

  if (error) {
    Serial.printf("[I18n] JSON parse error: %s\n", error.c_str());
    return false;
  }

  Serial.printf("[I18n] Loaded %s successfully\n", kI18nFilePath);
  return true;
}

void I18n::loadAvailableLanguages_() {
  JsonArray langs = doc_["meta"]["languages"];
  if (langs.isNull()) {
    Serial.println("[I18n] No languages array in meta");
    return;
  }

  langCount_ = 0;
  for (JsonVariant v : langs) {
    if (langCount_ >= kMaxLanguages) break;
    const char* lang = v.as<const char*>();
    if (lang) {
      availableLangs_[langCount_++] = lang;
      Serial.printf("[I18n] Available language: %s\n", lang);
    }
  }
}

void I18n::saveLanguagePreference_() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putString(kPrefsKeyLang, currentLang_);
    prefs.end();
  }
}

String I18n::loadLanguagePreference_() {
  Preferences prefs;
  String lang = "";
  if (prefs.begin(kPrefsNamespace, true)) {
    lang = prefs.getString(kPrefsKeyLang, "");
    prefs.end();
  }
  return lang;
}

JsonVariant I18n::lookupKey_(const char* key) {
  if (!key) return JsonVariant();

  // Parse hierarchical key: "category.subcategory.item"
  String keyStr = String(key);
  JsonVariant current = doc_.as<JsonVariant>();

  int startPos = 0;
  int dotPos = 0;

  while ((dotPos = keyStr.indexOf('.', startPos)) != -1) {
    String segment = keyStr.substring(startPos, dotPos);
    if (current.isNull()) return JsonVariant();
    current = current[segment];
    startPos = dotPos + 1;
  }

  // Last segment
  String lastSegment = keyStr.substring(startPos);
  if (current.isNull()) return JsonVariant();
  return current[lastSegment];
}
