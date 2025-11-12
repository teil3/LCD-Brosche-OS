# AGENTS.md — ESP32 Board-OS (GC9A01 + SD)

## Ziel
Kleines „OS“ mit App-Manager + getrennten Apps (z. B. Slideshow). Zwei Buttons: BTN1=System, BTN2=Appsteuerung.

## Projektstruktur
- ESP32-BoardOS.ino  — Setup/Loop, App-Registrierung
- Config.h           — Pins/Timings
- Core/              — App-Interface, AppManager, Buttons, Gfx, BootLogo, Storage
- Apps/SlideshowApp.*— Slideshow-Engine (SD/Flash)
- partitions.csv     — Custom-Partitionstabelle (16 MB, 8 MB LittleFS)

## Wichtige Pins
SCK=14, MOSI=15, MISO=2, SD_CS=13, TFT_CS=5, TFT_BL=22. BTN1=32, BTN2=33/35 (35 ohne internen Pull-up).

## Build & Test (nutze diese Kommandos)
- **Abhängigkeiten:** ESP32 Core ≥ 3.2.0, TFT_eSPI 2.5.43, TJpg_Decoder 1.1.0  
- **Arduino-CLI Setup (einmal):**
  - `arduino-cli core update-index`
  - `arduino-cli core install esp32:esp32`
  - `arduino-cli lib install "TFT_eSPI" "TJpg_Decoder"`
- **Kompilieren (Board-Beispiel):**
  - `arduino-cli compile -b esp32:esp32:esp32 --build-property build.flash_size=16MB --build-property build.partitions=partitions.csv --build-property upload.maximum_size=3670016 --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" --build-property compiler.c.extra_flags="-DSMOOTH_FONT" --build-path build-16m .`
  - `arduino-cli compile -b esp32:esp32:esp32 --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" --build-property compiler.c.extra_flags="-DSMOOTH_FONT" .`
- **Upload:**  
  - `arduino-cli upload  -b esp32:esp32:esp32 --input-dir build-16m -p /dev/ttyACM0`
- **Hinweis:** Vor dem Upload muss die erste Compile-Zeile gelaufen sein, damit die 16 MB `partitions.csv` aktiv ist. Der Upload nutzt immer das zuletzt erzeugte Artefakt.
- **Seriell (115200):**
  - `arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200`

**Build-Artefakte:** Für temporäre/experimentelle Builds ausschließlich `build-16m/` verwenden (bereits in `.gitignore`). Keine zusätzlichen Build-Verzeichnisse anlegen.

## Laufzeitregeln
- **SD vor TFT initialisieren**, beide CS anfangs HIGH.
- JPEG: `TJpgDec.setSwapBytes(true)`.
- LittleFS (`/slides`) wird beim Start eingebunden; grosse Dateien zuerst von SD in den Flash kopieren.
- Keine langen `delay()` in App-Logik (Buttons responsiv halten).
- Status/Toast-Overlays immer mit `TextRenderer::drawCentered` + Outline zeichnen und anschließend per `pauseUntil` ~1 s Animation pausieren, damit der Text sichtbar bleibt.

## Setup/Overlay-Architektur (Stand November 2025)
- BTN1 Doppel öffnet den **SystemUI**-Overlay (siehe `Core/SystemUI.*`). Dort laufen Setup, Quellenwahl, Transfer- und SD-Dialoge unabhängig vom Slideshow-Rendering.
- Solange SystemUI aktiv ist, wird `SlideshowApp::setUiLocked(true)` gesetzt; der AppManager zeichnet nicht, daher keine Slide-Überlagerungen oder Flackern.
- **Setup-Screen Stil** gilt für alle SystemUI-Seiten: weißer Titel, Auswahlzeilen in Weiß/Grau, Helper-Lines im kleinen Font unten mit 2 Zeilen (`BTN2 kurz …`, `BTN2 lang …`). Quellenwahl zeigt „SD-Karte“, „Flash“, „Exit“ im gleichen Layout.
- Quellenwechsel laufen über `SlideshowApp::setSlideSource(..., renderNow=false)`; erst nach Verlassen des Overlays wird die Diashow aktualisiert. Die zuletzt gewählte Quelle bleibt global aktiv (Slideshow setzt `source_` nicht mehr in `init()`).

### Offenes Refactoring
1. SD-Kopie-UI (Bestätigungs-/Fortschrittsoverlay) aus `SlideshowApp` in SystemUI verschieben.
2. BLE/USB-Transferanzeige ebenfalls nach SystemUI heben; anschließend alte ControlModes (Storage/BleReceive) entfernen.
3. Danach Slideshow final entrümpeln (nur Delete-/Render-Logik in der App belassen).

## Button-Mapping
- BTN1: Single=nextApp, Double=prevApp, Long=Backlight toggle
- BTN2 (Long wechselt Mode: Auto -> Manual -> Setup -> Auto)
  - Auto/Manual: Single=next Slide, Double=Dwell zyklisch, Triple=Filename an/aus
  - Setup: Single=Quelle SD/Flash toggeln, Double=Kopier-Dialog (Single Option wechseln, Long bestaetigen), Long=weiter zum naechsten Mode, Copy-Progress: BTN2 Long=Abbruch

## Was Codex tun darf
- Dateien in `Core/` & `Apps/` editieren/erstellen
- Neue App als `Apps/<Name>App.h/.cpp` anlegen und in `.ino` registrieren
- Build-Kommandos ausführen und Fehler beheben

## Bitte NICHT ändern
- Pinbelegung aus `Config.h` ohne Rückfrage
- TFT_eSPI User_Setup (funktionierender Stand)

## CRITICAL: Use ripgrep, not grep

NEVER use grep for project-wide searches (slow, ignores .gitignore). ALWAYS use rg.

- `rg "pattern"` — search content
- `rg --files | rg "name"` — find files
- `rg -t python "def"` — language filters

## File finding

- Prefer `fd` (or `fdfind` on Debian/Ubuntu). Respects .gitignore.

## JSON

- Use `jq` for parsing and transformations.

## Worklog 2025‑11‑12

- Quellenwahl-Screen lebt jetzt vollständig in `SystemUI`: Auswahl springt nicht mehr kurzzeitig in die Slideshow, da `SystemUI` nur noch für Transfer-Dialoge `ensureSlideshowActive()` erzwingt.
- BTN1 kurz im Setup-Overlay beendet nun den Overlay-Modus ohne App-Wechsel; Apps laufen direkt weiter wie beim Menüpunkt „Exit“.
- USB/BLE-Transfer läuft weiterhin über die Slideshow-Overlays und muss in einem nächsten Schritt in einen eigenen `SystemUI`-Screen gezogen werden.
- SD-Transfer-Workflow (Queue-Aufbau, Datei-Kopie, Confirm/Progress-UI) liegt komplett in `SystemUI`; `SlideshowApp` enthält keinen Copy-State mehr und wird nur noch via `setSource(Flash)` nach erfolgreichem Kopieren aktualisiert.
- SD-Kopieren-Confirm verwendet jetzt das Setup-Layout (vertikale Optionen, graue Nicht-Selektion) und der Fortschrittsbildschirm springt nach erfolgreicher Kopie automatisch zurück ins Setup (Status-Toast), während Fehler/Abbruch weiterhin im Overlay quittiert werden.
- Fortschrittsanzeige rendert nur noch inkrementelle Bereiche (Header, Balken, Helper-Lines) statt voller `fillScreen`, dadurch kein Flackern mehr während des Kopierens.
- Abbruch via BTN2 lang verlässt den Fortschritts-Screen sofort und zeigt den Status im Setup-Menü, identisch zum Verhalten nach erfolgreicher Kopie.
- USB/BLE-Transfer-Overlay ist in `SystemUI` umgezogen: Wenn du den Transfer-Screen aus dem Setup startest, `SystemUI` übernimmt Anzeige/Steuerung (BTN1 kurz/doppelt verlässt den Dialog) und aktualisiert Fortschritt/Texte direkt dort; außerhalb des Screens bleiben die Transfer-Funktionen deaktiviert.
- Solange ein SystemUI-Screen aktiv ist (Setup, Quellenwahl, SD-Dialoge) konsumiert `SystemUI::handleButton` sämtliche BTN-Events; BTN1 kann dadurch keine Apps mehr wechseln, während Overlays sichtbar sind.
- `arduino-cli compile -b esp32:esp32:esp32 --build-property build.flash_size=16MB --build-property build.partitions=partitions.csv --build-property upload.maximum_size=3670016 --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" --build-property compiler.c.extra_flags="-DSMOOTH_FONT" --build-path build-16m .` kompiliert erfolgreich (aktuell ~1.43 MB Text vs. 3.67 MB Limit).
