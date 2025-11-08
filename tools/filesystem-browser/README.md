# LittleFS Browser (WebSerial)

Web-App zum Inspizieren und Pflegen des LittleFS-Dateisystems der LCD-Brosche. Funktionen:

- 🔌 Verbinden via WebSerial (Chrome/Edge/Brave).
- 📂 Kompletten LittleFS-Inhalt inkl. Unterordnern auflisten (root, /system, /slides ...).
- 🗑️ Einzelne Dateien löschen – außer geschützten Ressourcen (`textapp.cfg`, `/system/font.vlw`, `/system/fonts/*`, `/system/bootlogo.jpg`).
- 📤 Dateien in beliebige vorhandene Ordner hochladen (nutzt `START ... END` wie die bestehenden Tools).
- ℹ️ FSINFO-Anzeige (Gesamt/Belegt/Frei).

## Starten

```bash
cd tools/filesystem-browser
python3 -m http.server 8080
```

Danach `http://localhost:8080` öffnen. Browser muss WebSerial unterstützen (Desktop Chrome/Edge/Brave/Opera). Vor dem Verbinden die Brosche in den USB-Transfer-Modus bringen (BTN2 lang in der Diashow bis „TRANSFER“ erscheint).

## Hinweise

- Die App verwendet den neuen `DELETE <pfad>`-Befehl der Firmware (siehe `Core/SerialImageTransfer.cpp`).
- `bootlogo.jpg` wird absichtlich ausgeblendet; `textapp.cfg`, `/system/font.vlw` und Schriften sind sichtbar, aber löschen ist deaktiviert.
- Uploads überschreiben bestehende Dateien (außer `/slides`, dort erzeugt die Firmware weiterhin eindeutige Namen).
- WebSerial benötigt HTTPS oder `localhost`. Unter Linux ggf. ModemManager stoppen oder Port freigeben (siehe `README_WebSerial.md`).
