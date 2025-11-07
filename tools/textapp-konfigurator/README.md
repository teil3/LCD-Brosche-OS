# TextApp Konfigurator für LCD-Brosche

Web-basiertes Tool zum grafischen Konfigurieren der TextApp. Ermöglicht einfaches Bearbeiten von Text, Farben, Modi und Schriftarten mit Live-Vorschau.

## Features

- 📝 **Grafische Benutzeroberfläche** für alle TextApp-Einstellungen
- 👁️ **Live-Vorschau** mit Animation auf rundem 240×240 Display
- 🎨 **Farbwähler** mit RGB565-Konvertierung
- 📥 **Konfiguration laden**:
  - Von lokaler Datei
  - Direkt von der Brosche per USB
- 📤 **Konfiguration hochladen**:
  - Per USB (WebSerial)
  - Per Bluetooth (WebBluetooth)
- 🔧 **Expert-Modus** für direktes Bearbeiten der textapp.cfg
- ⚡ **Keine Installation** nötig - läuft komplett im Browser

## Verwendung

### 1. Web-App starten

```bash
cd tools/textapp-konfigurator
python3 -m http.server 8080
```

Dann öffne: `http://localhost:8080`

### 2. Konfiguration erstellen/bearbeiten

#### Option A: Von Datei laden
1. Klicke "Von Datei laden"
2. Wähle eine bestehende `textapp.cfg`
3. Bearbeite die Einstellungen
4. Vorschau wird live aktualisiert

#### Option B: Von Brosche laden (USB)
1. **Brosche in Transfer-Modus** bringen:
   - In der Diashow-App BTN2 lang halten
   - Bis "TRANSFER" angezeigt wird
2. **USB verbinden**: Brosche per USB anschließen
3. Klicke "textapp.cfg von Brosche laden"
4. Wähle den Serial-Port
5. Konfiguration wird geladen und UI aktualisiert

#### Option C: Neu erstellen
1. Einfach mit den Standard-Einstellungen starten
2. Text, Farben, Modus etc. anpassen
3. Live-Vorschau beobachten

### 3. Konfiguration hochladen

#### USB-Upload
1. **Brosche in Transfer-Modus** (BTN2 lang in Diashow)
2. USB verbinden
3. Klicke "Über USB senden"
4. Wähle Serial-Port
5. Fertig!

#### Bluetooth-Upload
1. **Brosche in BLE-Modus** (BTN2 mehrmals lang in Diashow)
2. Klicke "Per Bluetooth senden"
3. Wähle "Brosche" aus der Liste
4. Fertig!

#### Download als Datei
- Klicke "Als textapp.cfg herunterladen"
- Datei kann manuell auf SD-Karte kopiert werden

## Konfigurationsoptionen

### Textinhalt
- Mehrzeilige Texteingabe
- `|` oder Enter für Zeilenumbrüche

### Anzeigemodus
- **Textblock**: Mehrzeiliger Text (mit Alignment-Option)
- **Große Wörter**: Zeigt Wörter nacheinander groß an
- **Große Buchstaben**: Zeigt Buchstaben nacheinander groß an

### Farben
- Textfarbe (RGB565)
- Hintergrundfarbe (RGB565)
- Grafischer Farbwähler oder Hex-Eingabe

### Schriftart
- FreeSans 18pt
- FreeSansBold 18pt
- FreeSansBold 24pt
- FreeSerif 18pt

### Weitere Optionen
- **Alignment** (nur bei Textblock): Links, Zentriert, Rechts
- **Geschwindigkeit** (nur bei BigWords/BigLetters): 10-10000 ms

## Expert-Modus

Klicke auf "🔧 Expert-Modus" um die `textapp.cfg` direkt als Text zu bearbeiten.

**Format:**
```
MODE=big_words
TEXT=Teil3 GmbH | online 3D-Drucken
COLOR=0xFFFF
BG_COLOR=0x0000
FONT=FreeSansBold24pt
ALIGN=center
SPEED=1000
```

## Browser-Kompatibilität

### WebSerial (USB-Transfer)
- ✅ Chrome, Edge, Brave, Opera (Desktop)
- ❌ Firefox, Safari
- ❌ Mobile Browser

### WebBluetooth (BLE-Transfer)
- ✅ Chrome, Edge, Opera, Brave
- ✅ Android Chrome
- ❌ Safari, iOS

## 🐧 Linux-Problem: WebSerial funktioniert nicht?

Siehe detaillierte Lösungen in [README_WebSerial.md](../../README_WebSerial.md) im Abschnitt "Troubleshooting: Linux WebSerial Probleme".

**Schnelltest:**
```bash
# ModemManager temporär stoppen
sudo systemctl stop ModemManager

# ESP32 ab- und wieder anstecken

# Jetzt sollte es funktionieren!
```

**Permanente Lösung:** ESP32 vom ModemManager blacklisten (siehe README_WebSerial.md)

## Technische Details

- **Version:** V0.3.1
- **Protokoll:** Gleiche USB/BLE-Protokolle wie Bildaufbereiter
- **Datei-Speicherort auf Brosche:** `/textapp.cfg` (LittleFS)
- **Firmware-Voraussetzung:** READ-Befehl (ab Commit mit SerialImageTransfer READ-Support)

## Verwandte Tools

- **Bildaufbereiter** (`../bildaufbereiter/`) - Bilder für die Brosche vorbereiten
- **upload_system_image.py** - Python-Script für manuelle Uploads

## Beispiel-Konfigurationen

### Große Wörter mit gelbem Hintergrund
```
MODE=big_words
TEXT=Teil3 | online 3D-Drucken | mach dein Teil!
COLOR=0x0000
BG_COLOR=0xFFE0
FONT=FreeSansBold24pt
SPEED=800
```

### Mehrzeiliger Text
```
MODE=text
TEXT=Zeile 1|Zeile 2|Zeile 3
COLOR=0xFFFF
BG_COLOR=0x001F
FONT=FreeSansBold18pt
ALIGN=center
```

### Große Buchstaben
```
MODE=big_letters
TEXT=HELLO
COLOR=0xF800
BG_COLOR=0x0000
FONT=FreeSansBold24pt
SPEED=500
```
