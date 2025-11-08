# ESP32 LCD-Brosche Board OS

<img src="docs/titelbild_brosche.png" alt="Board overview" width="50%">

Kompaktes Firmware-Projekt für eine tragbare LCD-Brosche: Ein schmuckartiges ESP32-Devboard mit rundem GC9A01-TFT (240x240) und SD-Karte. Die Firmware stellt einen einfachen App-Manager bereit, der mehrere eigenständige Apps kapselt (z. B. eine Slideshow) und über zwei Hardware-Tasten gesteürt wird.

## Tools

- [Bildauferbeiter](https://teil3.github.io/LCD-Brosche-OS/tools/bildaufbereiter/)
- [Text App Konfigurator](https://teil3.github.io/LCD-Brosche-OS/tools/textapp-konfigurator/)
- [Filesystem Browser](https://teil3.github.io/LCD-Brosche-OS/tools/filesystem-browser/)

### Buttonbelegung
- BTN1: Single -> nächste App, Double -> vorherige App, Long -> Backlight Toggle
- BTN2: (Moduswechsel mit Long-Press): Auto -> Manuell -> Setup -> Auto
  - Auto/Manuell: Single -> nächster Slide, Double -> Verweildauer zyklisch, Triple -> Dateiname an/aus
  - Setup-Modus (Flash/SD):
    - Single -> Quelle umschalten (SD <-> Flash)
    - Double -> Kopier-Dialog "Alles kopieren?" (Single wechselt Auswahl, Long bestätigt)
    - Long  -> zurück zum nächsten Modus (Auto)
    - Wärend des Kopierens: Long -> Abbrechen (Fortschrittsanzeige erscheint)
    
## Hardware
- ESP32 (D0WD-V3 oder kompatibel)
- GC9A01-basierte 1.28"-TFT-Anzeige
- Micro-SD-Kartenslot
- Zwei Taster (BTN1=GPIO32, BTN2=GPIO33 oder GPIO35 ohne internen Pull-up)
- Pins laut `Config.h` (nicht ändern, ohne Rücksprache):<br>`SCK=14`, `MOSI=15`, `MISO=2`, `SD_CS=13`, `TFT_CS=5`, `TFT_BL=22`
- Schmuck-tauglicher Formfaktor als Brosche mit Anhängepunkt

## Projektaufbau
- `ESP32-BoardOS.ino` - Einstiegspunkt, Hardware-Setup, App-Registrierung
- `Config.h` - Pinout, Timings, globale Konstanten
- `Core/` - App-Basis, AppManager, Button-Handling, Grafik-Utilities, Bootlogo
- `Apps/` - App-Implementierungen wie `SlideshowApp`
- `assets/` - Beispielinhalte für die SD-Karte (Bilder, Medien, ...)
- `docs/` - Hardwarefotos und ein Datenblatt als Referenz
- `partitions.csv` - Benutzerdefinierte Partitions-Tabelle (16 MB Flash, 8 MB LittleFS)

## Voraussetzungen
- Arduino CLI >= 0.35
- ESP32 Core >= 3.2.0 (`esp32:esp32`)
- Bibliotheken: `TFT_eSPI 2.5.43`, `TJpg_Decoder 1.1.0`

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "TFT_eSPI" "TJpg_Decoder"
```

## Kompilieren & Flashen
```bash
arduino-cli compile -b esp32:esp32:esp32 \
  --build-property build.flash_size=16MB \
  --build-property build.partitions=partitions.csv \
  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" \
  --build-property compiler.c.extra_flags="-DSMOOTH_FONT" \
  --build-path build-16m .
arduino-cli compile -b esp32:esp32:esp32 \
  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" \
  --build-property compiler.c.extra_flags="-DSMOOTH_FONT" .
arduino-cli upload  -b esp32:esp32:esp32 --input-dir build-16m -p /dev/ttyACM0
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

> Hinweis: Die Datei `partitions.csv` im Projektwurzelverzeichnis definiert die 8 MB grosse
> LittleFS-Partition. Führe vor dem Upload unbedingt die erste `arduino-cli compile`-Zeile
> aus, damit Partitionstabelle und Bootloader auf 16 MB Flash abgestimmt sind. Der direkte
> Upload-Befehl übernimmt dann das zuletzt erzeugte Build-Artefakt. Die zweite Compile-Zeile
> dient nur als Fallback für Boards mit 4 MB Standardlayout.

## Flash-Speicher & Offline-Modus
- LittleFS wird beim Start automatisch gemountet und bei Bedarf formatiert (erste Nutzung).
- Im Flash liegt der Ordner `/slides`, der durch den Kopiervorgang aus der Slideshow-App
  befüllt wird.
- Bilder lassen sich offline anzeigen, sobald sie von der SD-Karte in den Flash kopiert wurden.
- JPEGs sollten bereits am Rechner auf 204x240 Pixel verkleinert und als non-progressive
  gespeichert werden (z. B. per Web-Tool oder Skript), damit die ESP32-Dekodierung sicher klappt.

## Laufzeitverhalten
- SD-Karte vor dem TFT initialisieren; beide CS-Leitungen vor `begin()` auf HIGH legen.
- JPEG-Ausgabe nutzt `TJpgDec.setSwapBytes(true)`.
- Keine langen `delay()`-Aufrufe in App-Logik, um Buttons responsiv zu halten.
- Statusmeldungen (Toast/Overlay) immer via `TextRenderer::drawCentered()` + Outline zeichnen und mit `pauseUntil()` ungefähr 1 s sichtbar lassen.



## Offline-Slideshow (Flash)
- Schritt-für-Schritt (SD -> Flash):
  1. BTN2 lang drücken, bis der schwarze Setup-Bildschirm mit "Setup" und der aktuellen Quelle erscheint (Moduswechsel Auto -> Manual -> Setup).
  2. BTN2 doppelt drücken: Der Dialog "Alles kopieren?" erscheint.
  3. Mit BTN2 kurz zwischen "Nein" und "Ja" wechseln (Pfeil markiert die Auswahl), BTN2 lang bestätigt die markierte Option.
  4. BTN2 lang während des laufenden Kopierens -> Abbrechen (bereits kopierte Dateien bleiben erhalten).
  5. Nach Abschluss schaltet die App automatisch auf Flash als Quelle um (Setup-Overlay zeigt "Quelle: Flash").
- Quelle manuell wechseln (SD <-> Flash): Im Setup-Modus BTN2 einmal kurz drücken.
- Falls keine Bilder existieren, erscheint ein Hinweis Toast; in diesem Fall bleiben Quelle und Modus unverändert.

## Slideshow-App
- Lädt JPEG-Dateien von der SD-Karte (Standardverzeichnis `/`).
- Auto-Modus mit verschiedenen Verweildauern (1 s bis 5 min), umschaltbar per BTN2 Double.
- Optionaler Dateiname im Overlay; ein- oder ausblendbar mit BTN2 Triple.
- Nutzt Toast-Overlays, um Moduswechsel sichtbar zu machen.
- Medien müssen im JPEG-Format mit korrekter SOI-Signatur (`0xFF 0xD8`) vorliegen.

### Bildaufbereiter (Web-Tool)
- Online-Tool zum Zuschneiden (240×240), Encoden und Übertragen von Bildern:
  [https://teil3.github.io/LCD-Brosche-OS/tools/bildaufbereiter/](https://teil3.github.io/LCD-Brosche-OS/tools/bildaufbereiter/)
- Unterstützt Download als JPEG, Bluetooth-LE-Transfer sowie USB-WebSerial (921600 Baud).
- Funktioniert in Chromium-basierten Desktop-Browsern (HTTPS oder `localhost` erforderlich).
- USB-Senden: Browser fragt nach dem USB-Port, überträgt das JPEG in 1 KB-Blöcken, zeigt den Fortschritt an und schickt das Bild direkt in den Flash der Brosche.

## LittleFS-Tools
- `tools/upload_system_image.py /dev/ttyACM0 <datei> [/ziel]` – Überträgt Dateien nach LittleFS (Standard `/system`).
- `tools/list_littlefs.py --port /dev/ttyACM0 --root /system/fonts` – Listet rekursiv Inhalte und zeigt gleichzeitig mit `FSINFO` Total/Used/Free in KB an.
- Serielle Kommandos (USB-Transfer-Modus aktivieren, 115200 Bd):
  - `PING` → `USB OK PONG`
  - `LIST /system/fonts` → `USB OK LIST …` sowie `USB OK LISTDONE`
  - `FSINFO` → `USB OK FSINFO <total> <used> <free>` (Bytes)

## TextApp (SmoothFont)
- Nutzt ausschliesslich SmoothFonts (`*.vlw`), die auf LittleFS unter `/system/fonts/` liegen.
- Konfiguration via `/textapp.cfg` (Upload nach LittleFS). Wichtige Schlüssel:
  - `TEXT=Zeile 1|Zeile 2|…` → `|` erzeugt Zeilenumbrüche.
  - `FONT=FreeSansBold18pt` → Basisname ohne `.vlw` (Upload wandelt nach Kleinbuchstaben, z. B. `/system/fonts/freesansbold18pt.vlw`).
  - `ALIGN=left|center|right` → Startausrichtung (optional, Standard `center`).
  - `MODE=text|big_words|big_letters` → Initialer Modus (Standard `text`).
- Steuerung (BTN2, während die TextApp aktiv ist):
  - Single: Moduswechsel (Textblock → BigWords → BigLetters → …)
  - Double: Geschwindigkeit für BigWords/BigLetters (200/500/1000/2000/3000 ms)
  - Long: Im Textblock-Modus Ausrichtung zyklisch (links → zentriert → rechts)
- Textblock-Modus zeichnet den Absatz komplett in ein Sprite, skaliert ihn proportional, zentriert ihn vertikal und wendet die gewählte Ausrichtung an. BigWords/BigLetters skalieren SmoothFonts ebenfalls proportional (~80 % Breite bzw. ~70 % Höhe).
- Fallback: Fehlt der SmoothFont, erscheint eine Statusmeldung und es wird die interne pixelige Schrift genutzt.

### Fonts hochladen
```bash
python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSansBold18pt.vlw /system/fonts
python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSansBold24pt.vlw /system/fonts
python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSerif18pt.vlw /system/fonts
```
Prüfen, ob die Fonts vorhanden sind und wie viel Speicher noch frei ist:
```bash
python3 tools/list_littlefs.py --port /dev/ttyACM0 --root /system/fonts
```
Die Ausgabe enthält neben der Dateiliste auch `LittleFS: total/used/free` in KB (nutzt das neue `FSINFO`-Kommando).

## Dokumentation
- [Datenblatt (DE)](<docs/ESP32-1,28-Rund-TFT-Display-Board V1.12.pdf>) - Boarddatenblatt und Anschlussplan
- [User Manual (EN)](<docs/1.28ESP32-Round-TFT-Board_User-Manual_V1.12_EN.pdf>) - Englische Referenz zum Basismodul
- [Blockdiagramm](<docs/PrincipleBlockDiagram V1.11.pdf>) - Übersichtliche Darstellung von Signalflüsse und Komponenten

### Hardwaregalerie
<img src="docs/LCD-Brosche-vorne-Knoepfe.png" alt="Front view with buttons" width="50%">
<img src="docs/LCD-Brosche-links-SD-Card.png" alt="Left side with SD slot" width="50%">
<img src="docs/LCD-Brosche-unten-USB-C.png" alt="Bottom with USB-C" width="50%">
<img src="docs/LCD-Brosche-oben-Anhaenger.png" alt="Top with hanger" width="50%">

## Mitwirken & Nächste Schritte
- Weitere Apps können als `Apps/<Name>App.h/.cpp` hinzugefügt und in `ESP32-BoardOS.ino` registriert werden.
- Dokumentation ausbauen: Hardwareaufbau, Stromversorgung, bekannte Issues, FAQ.
- Tests oder Checks ergänzen (z. B. automatische SD-Content-Prüfung, Unit-Tests für Button-Events).
