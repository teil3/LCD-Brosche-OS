#include "I18n.h"

// Global instance
I18n i18n;

void I18n::begin() {
  #ifdef USB_DEBUG
    size_t freeBefore = ESP.getFreeHeap();
    Serial.printf("[I18n] begin() start, free heap: %u bytes\n", freeBefore);
  #endif

  ready_ = false;
  langIndex_ = 0;
  langCount_ = 0;
  clearTranslations_();

  // NEW APPROACH: Separate JSON file per language (i18n_de.json, i18n_en.json, etc.)
  // This dramatically reduces memory usage during language switching

  // Auto-detect available languages by scanning /system/ for i18n_*.json files
  File root = LittleFS.open("/system", "r");
  if (root && root.isDirectory()) {
    while (langCount_ < kMaxLanguages) {
      File entry = root.openNextFile();
      if (!entry) break;

      String filename = entry.name();
      entry.close();

      // Extract filename without path
      int lastSlash = filename.lastIndexOf('/');
      if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
      }

      // Check if filename matches pattern i18n_XX.json
      if (filename.startsWith("i18n_") && filename.endsWith(".json")) {
        // Extract language code (2 characters between i18n_ and .json)
        String langCode = filename.substring(5, filename.length() - 5);  // Remove "i18n_" and ".json"
        if (langCode.length() == 2) {
          strncpy(availableLangs_[langCount_], langCode.c_str(), sizeof(availableLangs_[langCount_]) - 1);
          availableLangs_[langCount_][sizeof(availableLangs_[langCount_]) - 1] = '\0';
          langCount_++;
        }
      }
    }
    root.close();
  }

  if (langCount_ == 0) {
    Serial.println("[I18n] ERROR: No i18n_*.json files found in /system/");
    return;
  }

  #ifdef USB_DEBUG
    Serial.printf("[I18n] Found %d languages: ", langCount_);
    for (uint8_t i = 0; i < langCount_; i++) {
      Serial.printf("%s%s", availableLangs_[i], (i < langCount_ - 1) ? ", " : "\n");
    }
  #endif

  // Load saved language preference
  String savedLang = loadLanguagePreference_();
  if (savedLang.isEmpty()) {
    savedLang = "de";  // Default
  }

  // Find language index
  langIndex_ = 0;
  for (uint8_t i = 0; i < langCount_; i++) {
    if (strcmp(availableLangs_[i], savedLang.c_str()) == 0) {
      langIndex_ = i;
      break;
    }
  }
  strncpy(currentLang_, savedLang.c_str(), sizeof(currentLang_) - 1);

  // Load translations for current language from separate file
  if (!loadLanguage_()) {
    Serial.printf("[I18n] Failed to load language: %s\n", currentLang_);
    return;
  }

  ready_ = true;

  #ifdef USB_DEBUG
    size_t freeAfter = ESP.getFreeHeap();
    Serial.printf("[I18n] Ready. Language: %s (%d of %d), %u translations cached\n",
                  currentLang_, langIndex_ + 1, langCount_, translations_.size());
    Serial.printf("[I18n] Free heap: %u bytes (used: %d bytes)\n",
                  freeAfter, (int)(freeBefore - freeAfter));
  #endif
}

const char* I18n::t(const char* key) {
  if (!ready_ || !key) {
    return key ? key : "";
  }

  const char* translation = lookupTranslation_(key);
  if (translation) {
    return translation;
  }

  #ifdef USB_DEBUG
    Serial.printf("[I18n] Missing key: %s\n", key);
  #endif
  return key;  // Fallback: return the key itself
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
  uint8_t newIndex = 255;
  for (uint8_t i = 0; i < langCount_; i++) {
    if (strcmp(availableLangs_[i], lang) == 0) {
      newIndex = i;
      break;
    }
  }

  if (newIndex == 255) {
    Serial.printf("[I18n] Language not found: %s\n", lang);
    return false;
  }

  // Only reload if language actually changed
  if (newIndex == langIndex_) {
    return true;
  }

  langIndex_ = newIndex;
  strncpy(currentLang_, lang, sizeof(currentLang_) - 1);
  currentLang_[sizeof(currentLang_) - 1] = '\0';

  // Reload translations for new language
  Serial.printf("[I18n] Reloading translations for language: %s (index %d)\n", lang, newIndex);
  ready_ = false;  // Mark as not ready during reload

  // Clear old translations AND free the array to maximize available heap
  clearTranslations_();

  // IMPORTANT: Force garbage collection to free any fragmented memory
  delay(10);  // Give system time to settle

  Serial.printf("[I18n] Free heap before loadLanguage_(): %u bytes\n", ESP.getFreeHeap());

  if (!loadLanguage_()) {
    Serial.printf("[I18n] Failed to reload language: %s\n", lang);
    return false;
  }
  ready_ = true;  // Mark as ready after successful reload

  saveLanguagePreference_();
  Serial.printf("[I18n] Language changed to: %s (index %d), %u translations loaded\n",
                lang, newIndex, translations_.size());
  return true;
}

const char* I18n::currentLanguage() const {
  return currentLang_;
}

bool I18n::loadLanguage_() {
  #ifdef USB_DEBUG
    size_t freeBefore = ESP.getFreeHeap();
    Serial.printf("[I18n] loadLanguage_() start, free heap: %u bytes\n", freeBefore);
  #endif

  // Build the filename for the current language: /system/i18n_XX.json
  char filename[32];
  snprintf(filename, sizeof(filename), "/system/i18n_%s.json", currentLang_);

  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("[I18n] Failed to open: %s\n", filename);
    return false;
  }

  // Get file size
  size_t fileSize = file.size();
  Serial.printf("[I18n] %s file size: %u bytes\n", filename, fileSize);

  // Parse the JSON file (much smaller now - ~4KB instead of 10KB!)
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[I18n] JSON parse error: %s (free heap: %u bytes)\n",
                  error.c_str(), ESP.getFreeHeap());
    return false;
  }

  Serial.printf("[I18n] JSON parsed successfully, free heap: %u bytes\n", ESP.getFreeHeap());

  extractTranslations_(doc);
  // doc is freed automatically when it goes out of scope

  #ifdef USB_DEBUG
    size_t freeAfter = ESP.getFreeHeap();
    Serial.printf("[I18n] loadLanguage_() end, free heap: %u bytes (used: %d bytes)\n",
                  freeAfter, (int)(freeBefore - freeAfter));
  #endif

  return true;
}

void I18n::extractTranslations_(JsonDocument& doc) {
  // Clear existing translations
  translations_.clear();

  Serial.printf("[I18n] Extracting translations (dynamic vector), free heap: %u\n", ESP.getFreeHeap());

  // NEW SIMPLER STRUCTURE: Each file has direct key-value pairs, no arrays!
  // Example: {"apps": {"slideshow": "Diashow", "lua": "Lua"}, ...}

  // Recursive function to extract all translations
  auto extractLevel = [this](JsonVariant obj, const String& prefix, auto& self) -> void {
    if (!obj.is<JsonObject>()) return;

    for (JsonPair kv : obj.as<JsonObject>()) {
      String key = prefix.isEmpty() ? String(kv.key().c_str()) : prefix + "." + kv.key().c_str();

      JsonVariant val = kv.value();
      if (val.is<String>() || val.is<const char*>()) {
        // This is a direct translation value!
        const char* jsonValue = val.as<const char*>();
        if (!jsonValue) jsonValue = "";

        // Store the translation (vector grows dynamically)
        TranslationEntry entry;
        entry.key = String(key.c_str());
        entry.value = String(jsonValue);
        translations_.push_back(entry);
      } else if (val.is<JsonObject>()) {
        // Recurse into nested object
        self(val, key, self);
      }
      // Ignore other types (arrays, numbers, etc.)
    }
  };

  extractLevel(doc.as<JsonVariant>(), "", extractLevel);

  #ifdef USB_DEBUG
    Serial.printf("[I18n] Extracted %u translations for language %s\n",
                  translations_.size(), currentLang_);
  #endif
}

void I18n::clearTranslations_() {
  #ifdef USB_DEBUG
    Serial.printf("[I18n] clearTranslations_() clearing %u entries, free heap before: %u\n",
                  translations_.size(), ESP.getFreeHeap());
  #endif
  // Clear vector and free all memory
  translations_.clear();
  translations_.shrink_to_fit();  // Release memory back to heap
  #ifdef USB_DEBUG
    Serial.printf("[I18n] clearTranslations_() done, free heap after: %u\n",
                  ESP.getFreeHeap());
  #endif
}

const char* I18n::lookupTranslation_(const char* key) {
  if (!key) {
    return nullptr;
  }

  for (size_t i = 0; i < translations_.size(); i++) {
    if (translations_[i].key.equals(key)) {
      return translations_[i].value.c_str();
    }
  }

  return nullptr;
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

