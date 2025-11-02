# System Image Upload Tool

Dieses Tool ermöglicht das Hochladen von System-Ressourcen (z.B. Bootlogo) ins ESP32 LittleFS `/system/` Verzeichnis.

## Warum `/system/`?

- **Getrennt von `/slides/`**: Slideshow-Bilder werden in `/slides/` gespeichert
- **Geschützt vor Löschung**: Die Löschfunktion der Slideshow-App betrifft nur `/slides/`, nicht `/system/`
- **Spart Flash-Speicher**: System-Ressourcen werden aus dem Programmspeicher (PROGMEM) ins LittleFS verschoben

## Verwendung

### 1. Python-Abhängigkeiten installieren

```bash
pip install pyserial
```

### 2. ESP32 flashen (mit USB-Empfang aktiviert)

Kompiliere und flashe die Firmware:

```bash
arduino-cli compile -b esp32:esp32:esp32 \
  --build-property build.flash_size=16MB \
  --build-property build.partitions=partitions.csv \
  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT" \
  --build-property compiler.c.extra_flags="-DSMOOTH_FONT" \
  --build-path build-16m .

arduino-cli upload -b esp32:esp32:esp32 --input-dir build-16m -p /dev/ttyACM0
```

### 3. USB-Empfangsmodus aktivieren

Auf dem ESP32:
1. Starte die Slideshow-App
2. **Doppelklick BTN1** → Wechsel zum Storage-Menu
3. **Doppelklick BTN1** → Wechsel zu "USB Empfang"
4. Display zeigt: "USB Empfang bereit"

### 4. Bootlogo hochladen

```bash
python3 tools/upload_system_image.py /dev/ttyACM0 assets/boot_logo_200.jpg
```

**Ausgabe:**
```
============================================================
ESP32 System Image Upload Tool
============================================================

📁 File: boot_logo_200.jpg
📊 Size: 12345 bytes (12.1 KB)
📂 Target: /system/boot_logo_200.jpg
🔌 Port: /dev/ttyACM0

🔗 Opening serial connection...
🏓 Sending PING...
✅ PONG received
📤 Sending: START 12345 boot_logo_200.jpg /system
📥 Response: USB OK START boot_logo_200.jpg 12345
📤 Uploading data...
   10% (1234/12345 bytes)
   20% (2468/12345 bytes)
   ...
   100% (12345/12345 bytes)
✅ Upload complete: 12345 bytes sent
📤 Sending END command...
📥 Response: USB OK END boot_logo_200.jpg 12345
✅ SUCCESS: File uploaded to /system/boot_logo_200.jpg

============================================================
✅ Upload successful!
============================================================
```

### 5. ESP32 neu starten

Das Bootlogo wird nun beim Start aus `/system/bootlogo.jpg` geladen!

## Erweiterte Verwendung

### Anderes Zielverzeichnis

```bash
# Upload in anderes Verzeichnis
python3 tools/upload_system_image.py /dev/ttyACM0 myimage.jpg /custom
```

### Mehrere Dateien hochladen

```bash
# Bootlogo
python3 tools/upload_system_image.py /dev/ttyACM0 assets/boot_logo_200.jpg

# Rename zu bootlogo.jpg (wird automatisch erkannt)
# Manuell auf ESP32 umbenennen ODER:
```

**Tipp:** Benenne das Bild vor dem Upload zu `bootlogo.jpg` um:

```bash
cp assets/boot_logo_200.jpg /tmp/bootlogo.jpg
python3 tools/upload_system_image.py /dev/ttyACM0 /tmp/bootlogo.jpg
```

## Protokoll-Details

Das Script nutzt das erweiterte SerialImageTransfer-Protokoll:

```
START <size> <filename> [directory]
<binary data>
END
```

**Beispiel:**
```
START 12345 bootlogo.jpg /system
<12345 bytes binary JPEG data>
END
```

**Antworten:**
- `USB OK START bootlogo.jpg 12345` → Transfer gestartet
- `USB OK END bootlogo.jpg 12345` → Transfer erfolgreich
- `USB ERR <code> <message>` → Fehler

## Flash-Speicher freigeben (Optional)

Nach erfolgreichem Upload kann das PROGMEM-Array aus `Core/BootLogo.h` entfernt werden:

1. Öffne `Core/BootLogo.h`
2. Lösche Zeilen 15-296 (das gesamte `schmucklogo_jpg[]` Array)
3. Entferne den Fallback-Code in `drawBootLogo()` (Zeile 334-337)
4. Neu kompilieren → **~10KB Flash-Speicher gespart!**

**Vor dem Löschen sicherstellen:**
- ✅ Bootlogo wurde erfolgreich hochgeladen
- ✅ ESP32 startet mit dem Logo aus LittleFS
- ✅ Kein Fallback mehr nötig

## Fehlerbehebung

### "No PONG response"
- ESP32 ist nicht im USB-Empfangsmodus
- Falsche Baud-Rate (muss 921600 sein)
- Falscher Port

### "USB ERR DISABLED"
- USB-Empfangsmodus nicht aktiviert
- Siehe Schritt 3

### "File not found"
- Dateipfad falsch
- Verwende absoluten Pfad oder relativen Pfad von Projekt-Root

### "Permission denied" (Linux)
```bash
# User zur dialout-Gruppe hinzufügen
sudo usermod -a -G dialout $USER
# Logout/Login erforderlich
```

## Siehe auch

- `CLAUDE.md` - Hauptdokumentation
- `Core/SerialImageTransfer.h` - Protokoll-Implementierung
- `Core/BootLogo.h` - Bootlogo-Logik
