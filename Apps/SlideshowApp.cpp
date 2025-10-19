#include "SlideshowApp.h"

#include <SD.h>
#include <TJpg_Decoder.h>
#include <algorithm>
#include <array>

#include "Core/Gfx.h"
#include "Config.h"

// ---- intern ----

namespace {
constexpr std::array<uint32_t, 5> kDwellSteps{1000, 5000, 10000, 30000, 300000};
}

bool SlideshowApp::isJpeg_(const String& n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

void SlideshowApp::showModeOsd_() {
  // Kleine Statuszeile oben mit dem aktuellen Modus oder Override
  const uint32_t now = millis();
  const bool useOverride = osdOverrideUntil_ && (now < osdOverrideUntil_);
  if (!useOverride && osdOverrideUntil_) {
    osdOverrideUntil_ = 0;
    osdOverride_.clear();
  }

  String txt = useOverride ? osdOverride_ : modeLabel_();
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

  drawToastIfActive_();

  Serial.printf("[Slideshow] OK: %s\n", path.c_str());
}

void SlideshowApp::advance_(int step) {
  if (files_.empty()) return;

  const size_t total = files_.size();
  int64_t next = static_cast<int64_t>(idx_) + step;
  int64_t wrap = static_cast<int64_t>(total);
  next %= wrap;
  if (next < 0) next += wrap;

  idx_ = static_cast<size_t>(next);
  showCurrent_();
  timeSinceSwitch_ = 0;
}

void SlideshowApp::applyDwell_() {
  if (dwellIdx_ >= kDwellSteps.size()) dwellIdx_ = 0;
  dwell_ms = kDwellSteps[dwellIdx_];
}

void SlideshowApp::setOsdOverride_(const String& txt, uint32_t duration_ms) {
  osdOverride_ = txt;
  osdOverrideUntil_ = millis() + duration_ms;
}

String SlideshowApp::dwellLabel_() const {
  if (dwell_ms % 60000 == 0) {
    return String(dwell_ms / 60000) + "m";
  }
  return String((dwell_ms + 500) / 1000) + "s";
}

String SlideshowApp::modeLabel_() const {
  if (!auto_mode) return String("MANUAL");
  return String("AUTO ") + dwellLabel_();
}

void SlideshowApp::showToast_(const String& txt, uint32_t duration_ms) {
  toastText_ = txt;
  toastUntil_ = millis() + duration_ms;
}

void SlideshowApp::drawToastIfActive_() {
  if (!toastUntil_) return;
  if (millis() >= toastUntil_) return;

  const int boxW = TFT_W - 40;
  const int boxH = 32;
  const int x = (TFT_W - boxW) / 2;
  const int y = (TFT_H - boxH) / 2;

  tft.fillRoundRect(x, y, boxW, boxH, 6, TFT_BLACK);
  tft.drawRoundRect(x, y, boxW, boxH, 6, TFT_YELLOW);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(toastText_, TFT_W/2, TFT_H/2, 2);
}

// ---- App-Lebenszyklus ----

void SlideshowApp::init() {
  files_.clear();
  idx_ = 0;
  timeSinceSwitch_ = 0;
  auto_mode = true;  // Start immer im Auto-Slide
  osdOverride_.clear();
  osdOverrideUntil_ = 0;
  toastText_.clear();
  toastUntil_ = 0;
  applyDwell_();

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
    timeSinceSwitch_ = 0;
    // idx_ bleibt auf 0; Auto-Advance übernimmt tick()
  }
}

void SlideshowApp::tick(uint32_t delta_ms) {
  uint32_t now = millis();

  if (osdOverrideUntil_ && now >= osdOverrideUntil_) {
    osdOverrideUntil_ = 0;
    osdOverride_.clear();
    showModeOsd_();
  }

  if (toastUntil_ && now >= toastUntil_) {
    toastUntil_ = 0;
    if (!files_.empty()) {
      showCurrent_();
    } else {
      showModeOsd_();
    }
  }

  if (files_.empty() || !auto_mode) return;

  timeSinceSwitch_ += delta_ms;
  if (timeSinceSwitch_ >= dwell_ms) {
    advance_(+1);
  }
}

void SlideshowApp::onButton(uint8_t index, BtnEvent e) {
  if (index != 2 || files_.empty()) return;

  switch (e) {
    case BtnEvent::Single: // nächstes Bild
      advance_(+1);
      break;

    case BtnEvent::Double: // dwell-Zeit zyklisch erhöhen
      dwellIdx_ = (dwellIdx_ + 1) % kDwellSteps.size();
      applyDwell_();
      timeSinceSwitch_ = 0;
      setOsdOverride_(String("AUTO ") + dwellLabel_(), 1000);
      showToast_(String("AUTO ") + dwellLabel_(), 1000);
      showCurrent_();
      Serial.printf("[Slideshow] dwell=%lu ms\n", (unsigned long)dwell_ms);
      break;

    case BtnEvent::Triple: // Dateiname toggeln
      show_filename = !show_filename;
      showCurrent_();
      timeSinceSwitch_ = 0;
      break;

    case BtnEvent::Long: // Moduswechsel: AUTO <-> MANUAL
      auto_mode = !auto_mode;
      // Komplettes Bild inkl. OSD neu zeichnen, damit kein schwarzer Balken bleibt
      osdOverrideUntil_ = 0;
      osdOverride_.clear();
      showCurrent_();
      timeSinceSwitch_ = 0;
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
