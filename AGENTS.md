# AGENTS.md — ESP32 Board-OS (GC9A01 + SD)

## Ziel
Kleines „OS“ mit App-Manager + getrennten Apps (z. B. Slideshow). Zwei Buttons: BTN1=System, BTN2=Appsteuerung.

## Projektstruktur
- ESP32-BoardOS.ino  — Setup/Loop, App-Registrierung
- Config.h           — Pins/Timings
- Core/              — App-Interface, AppManager, Buttons, Gfx, BootLogo
- Apps/SlideshowApp.*— Slideshow-Engine (JPEG von SD)

## Wichtige Pins
SCK=14, MOSI=15, MISO=2, SD_CS=13, TFT_CS=5, TFT_BL=22. BTN1=32, BTN2=33/35 (35 ohne internen Pull-up).

## Build & Test (nutze diese Kommandos)
- **Abhängigkeiten:** ESP32 Core ≥ 3.2.0, TFT_eSPI 2.5.43, TJpg_Decoder 1.1.0  
- **Arduino-CLI Setup (einmal):**
  - `arduino-cli core update-index`
  - `arduino-cli core install esp32:esp32`
  - `arduino-cli lib install "TFT_eSPI" "TJpg_Decoder"`
- **Kompilieren (Board-Beispiel):**
  - `arduino-cli compile -b esp32:esp32:esp32 .`
- **Seriell (115200):**
  - `arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200`

## Laufzeitregeln
- **SD vor TFT initialisieren**, beide CS anfangs HIGH.
- JPEG: `TJpgDec.setSwapBytes(true)`.
- Keine langen `delay()` in App-Logik (Buttons responsiv halten).

## Button-Mapping
- BTN1: Single=nextApp, Double=prevApp, Long=Backlight toggle
- BTN2: Single=next, Double=prev, Triple=Filename an/aus, Long=Auto↔Manuell

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
