# assets/fonts

Hier liegen die SmoothFonts im `.vlw`-Format, die nach dem Flashen über das Upload-Script auf das LittleFS kopiert werden.

- `font.vlw` bleibt die Standardschrift der Hauptoberfläche (`TextRenderer::begin()` lädt sie aus `/system/font.vlw`).
- Zusätzliche TextApp-Schriften erwartet die Firmware unter `/system/fonts/<Name>.vlw` (z. B. `FreeSansBold24pt.vlw`).
- Die Konfiguration (`FONT=...`) verwendet den Basisnamen ohne Pfad und Endung; die Datei muss zuvor mit `tools/upload_system_image.py` übertragen werden.

Hinweis: Keine automatische Synchronisation – Uploads müssen nach jedem Firmware-Flash erneut durchgeführt werden.
