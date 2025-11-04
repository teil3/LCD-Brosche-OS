# assets/fonts

Hier liegen die SmoothFonts im `.vlw`-Format, die zur Laufzeit in LittleFS unter `/system` abgelegt und anschließend über `TextRenderer::begin()` mit `tft.loadFont(...)` geladen werden.

- `font.vlw` ist die Standard-Schrift der Hauptoberfläche und wird via `tools/upload_system_image.py` hochgeladen.
- Weitere `.vlw`-Dateien dienen als alternative Schriften oder Backups und können bei Bedarf ausgetauscht werden, ohne die Firmware neu zu bauen.

Hinweis: Diese Dateien werden nicht automatisch ins Flash kopiert – der Upload muss nach dem Flashen der Firmware separat erfolgen.
