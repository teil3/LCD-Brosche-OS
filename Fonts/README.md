# Fonts

Dieses Verzeichnis enthält die GFX-FreeFonts, die beim Kompilieren direkt in den Sketch eingebunden werden.

- `GFXFF/` stellt die `GFXfont`-Header bereit, auf die die TextApp über `setFreeFont(...)` zugreift.
- Die Fonts liegen im PROGMEM und benötigen keine Dateien auf dem Dateisystem des ESP32.
- Das Flag `LOAD_GFXFF` in `Core/Gfx.h` stellt sicher, dass diese Bitmap-Fonts in TFT_eSPI verfügbar sind.
- Aktuell sind `FreeSans18pt`, `FreeSansBold18pt`, `FreeSansBold24pt` und `FreeSerif18pt` eingebunden (s. `GFXFF/gfxfont.h`).

⚠️ Änderungen an den Dateien hier erfordern ein erneutes Kompilieren der Firmware. Für Laufzeit-Fonts, die per LittleFS geladen werden, siehe `assets/fonts`.
