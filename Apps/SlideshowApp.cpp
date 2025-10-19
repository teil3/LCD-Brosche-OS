#include "SlideshowApp.h"

#include <SD.h>
#include <TJpg_Decoder.h>
#include <algorithm>

#include "Core/Gfx.h"
#include "Config.h"

// ---- intern ----

bool SlideshowApp::isJpeg_(const String& n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

void SlideshowApp::showModeOsd_() {
  // Kleine Statuszeile oben mit dem aktuellen Modus
  const char* txt = auto_mode ? "AUTO" : "MANUAL";
  tft.fillRect(0, 0, TFT_W, 18, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(txt, TFT_W/2, 2, 2);
}

void SlideshowApp::showCurrent_() {
  if (files_.empty()) return;

  const String& path = files_[idx_];

  // Kurzer SOI-Check
  File test = SD.open(path.c_str(), FILE_READ);
  if (!test) {
    Serial.printf("[Slideshow] open FAIL: %s\n", path.c_str());
    return;
  }
  uint8_t sig[2] = {0,0};
  size_t n = test.read(sig, 2);
  test.close();
  if (n != 2 || sig[0] != 0xFF || sig[1] != 0xD8) {
    Serial.printf("[Slideshow] WARN SOI: %s\n", path.c_str());
    return;
  }

  // Zeichnen
  tft.fillScreen(TFT_BLACK);
  int rc = TJpgDec.drawSdJpg(0, 0, path.c_str());
  if (rc != 1) {
    Serial.printf("[Slideshow] drawSdJpg fail (%d): %s\n", rc, path.c_str());
    return;
  }

  // Modus-OSD oben
  showModeOsd_();

  // Dateiname unten (optional)
  if (show_filename) {
    tft.fillRect(0, TFT_H-18, TFT_W, 18, TFT_BLACK);
    tft.setTextDatum(BC_DATUM);
    String base = path;
    int sidx = base.lastIndexOf('/');
    if (sidx >= 0) base = base.substring(sidx+1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(base, TFT_W/2, TFT_H-2, 2);
  }

  Serial.printf("[Slideshow] OK: %s\n", path.c_str());
}

// ---- App-Lebenszyklus ----

void SlideshowApp::init() {
  files_.clear();
  idx_ = 0;
  lastSwitch_ = 0;

  File root = SD.open(dir.c_str());
  if (!root || !root.isDirectory()) {
    Serial.printf("[Slideshow] Ordner fehlt: %s\n", dir.c_str());
    return;
  }

  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    String base = f.name();
    int s = base.lastIndexOf('/');
    if (s >= 0) base = base.substring(s+1);
    if (!isJpeg_(base)) continue;

    String full = dir;
    if (!full.endsWith("/")) full += "/";
    full += base;
    files_.push_back(full);
  }

  std::sort(files_.begin(), files_.end());
  Serial.printf("[Slideshow] %u Bilder in %s\n", (unsigned)files_.size(), dir.c_str());

  tft.fillScreen(TFT_BLACK);
  // Beim Start sofort erstes Bild anzeigen (falls vorhanden)
  if (!files_.empty()) {
    showCurrent_();
    lastSwitch_ = millis();
    // idx_ bleibt auf 0; Auto-Advance übernimmt tick()
  }
}

void SlideshowApp::tick(uint32_t /*delta_ms*/) {
  if (files_.empty()) return;

  // Nur automatisch weiterschalten, wenn Auto-Modus aktiv ist
  if (auto_mode && (millis() - lastSwitch_ >= dwell_ms)) {
    idx_ = (idx_ + 1) % files_.size();
    showCurrent_();
    lastSwitch_ = millis();
  }
}

void SlideshowApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2 || files_.empty()) return;

  switch (e) {
    case BtnEvent::Single: // nächstes Bild
      idx_ = (idx_ + 1) % files_.size();
      showCurrent_();
      lastSwitch_ = millis();
      break;

    case BtnEvent::Double: // vorheriges Bild
      idx_ = (idx_ + files_.size() - 1) % files_.size();
      showCurrent_();
      lastSwitch_ = millis();
      break;

    case BtnEvent::Triple: // Dateiname toggeln
      show_filename = !show_filename;
      showCurrent_();
      lastSwitch_ = millis();
      break;

    case BtnEvent::Long: // Moduswechsel: AUTO <-> MANUAL
      auto_mode = !auto_mode;
      // OSD aktualisieren; Timer resetten, damit AUTO nicht sofort triggert
      showModeOsd_();
      lastSwitch_ = millis();
      Serial.printf("[Slideshow] Mode: %s\n", auto_mode ? "AUTO" : "MANUAL");
      break;

    default:
      break;
  }
}

void SlideshowApp::draw() {
  // NOP – JPEGs werden direkt in showCurrent_ gezeichnet
}

void SlideshowApp::shutdown() {
  files_.clear();
}

