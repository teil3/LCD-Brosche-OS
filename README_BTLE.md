# 📡 Idee: Bildübertragung via Bluetooth Low Energy (BLE) zum ESP32

## 🧠 Ziel
Bilder, die im **Browser** mit dem bestehenden Tool `bildaufbereiter` erstellt werden, sollen **kabellos über Bluetooth LE** an den **ESP32** übertragen werden, damit sie dort auf der **LCD-Brosche** angezeigt werden können.

Der komplette Workflow läuft **offline**, direkt zwischen Browser und ESP32:

```
Browser (GitHub-Pages-App)
   ↓ WebBluetooth (GATT)
ESP32 (BLE-Peripheral mit Image-Service)
   ↓
SPIFFS / SD / Display
```

---

## 🔧 Technisches Konzept

### 1. ESP32 (Peripheral / Server)
Der ESP32 agiert als **BLE-Peripheral** mit einem eigenen GATT-Service z. B.  
`ImageTransferService`.

| Element | Beispiel-UUID | Funktion |
|----------|----------------|----------|
| Service | `12345678-1234-1234-1234-1234567890ab` | Bilder empfangen |
| Characteristic `chunk` | `abcd0001-...` | Daten-Chunks (20 Bytes pro Write) |
| Characteristic `control` | `abcd0002-...` | Steuerbefehle (Start, Ende, Status) |

**Ablauf:**
1. Der Browser verbindet sich und sendet ein *Start-Signal* (Dateigrösse, Name).
2. Der Browser schreibt das JPEG in **20-Byte-Chunks** in die `chunk`-Characteristic.  
   (BLE-MTU = 23 Bytes → 20 Bytes Nutzlast)
3. Der ESP32 fügt die Daten zusammen (z. B. in `SPIFFS`)  
   und prüft am Ende CRC oder „End of Transmission“.
4. Optional zeigt der ESP32 den Fortschritt über eine LED oder auf dem Display.
5. Nach vollständigem Empfang kann das JPEG direkt mit `TJpg_Decoder` oder `JPEGDEC` angezeigt werden.

---

### 2. Browser (Central / Client)
Die bestehende Web-App `bildaufbereiter` (läuft auf GitHub Pages) wird um eine **WebBluetooth-Funktion** erweitert.

**Funktion:**
- Button: „📤 An ESP32 senden (BLE)“
- Öffnet per `navigator.bluetooth.requestDevice()` die Verbindung:
  ```js
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'Brosche' }],
    optionalServices: ['12345678-1234-1234-1234-1234567890ab']
  });
  ```
- Stellt Verbindung her, ermittelt die `chunk`-Characteristic.
- Liest das erzeugte JPEG aus (z. B. als ArrayBuffer) und sendet es in Paketen à 20 Bytes:
  ```js
  for (let i = 0; i < data.byteLength; i += 20) {
    const slice = data.slice(i, i + 20);
    await chunkCharacteristic.writeValue(slice);
  }
  ```
- Sendet abschliessend ein *End-Signal* über die `control`-Characteristic.

Browser-seitig ist dafür **nur HTTPS oder localhost** erlaubt (WebBluetooth-Security).

---

## ⚙️ Geschwindigkeits-Einschätzung
| ESP32-Variante | BLE-Version | reale Geschwindigkeit | 20 KB-JPEG-Transfer |
|----------------|-------------|-----------------------|--------------------|
| ESP32 Classic | BLE 4.2 | ~ 10 – 25 KB/s | ~ 1 – 2 Sekunden |
| ESP32-S3 / C3 | BLE 5 | ~ 30 – 80 KB/s | ~ 0.3 – 0.7 Sekunden |

Für kleine JPEGs (240×240 px ≈ 15–25 KB) völlig ausreichend.

---

## 🧩 Vorteile
- Kein Wi-Fi, kein Pairing nötig.  
- Funktioniert direkt aus dem Browser (WebBluetooth).  
- Geringe Leistungsaufnahme.  
- Sicher (GATT-Zugriff nur nach Benutzer-Bestätigung).  
- Ideal für kleine Bilder / Icons.

---

## ⚠️ Einschränkungen
- BLE ist nicht für grosse Datenmengen optimiert (> 200 KB wird langsam).  
- Browser-Unterstützung: Chrome, Edge, Opera, Brave, Android Chrome.  
  (Safari/iOS derzeit keine WebBluetooth-API.)
- Übertragung nur über HTTPS (GitHub Pages ist kompatibel).

---

## 🧱 Nächste Schritte

1. **ESP32-Firmware**
   - Service + Characteristics definieren  
   - Chunk-Buffering und Speicherung (SPIFFS/SD)  
   - JPEG anzeigen (z. B. `TJpg_Decoder.drawJpg(...)`)

2. **Web-App**
   - WebBluetooth-Integration in `bildaufbereiter`  
   - Fortschrittsbalken, Cancel-Button  
   - Option: Gerät merken (per `device.id`)

3. **Testen**
   - Mit ESP32-S3 DevKitC und Chrome testen  
   - Transfer-Zeit, Verbindungsstabilität, Timeout-Handling

---

## 💡 Erweiterungen (später)
- Automatische Vorschau auf dem ESP-Display nach Empfang  
- Komprimierte Blockübertragung (z. B. RLE oder Base64)  
- Zweiter BLE-Service für Steuerbefehle (z. B. Brightness / Next Image)  
- Optionales Pairing + Whitelist (Sicherheitsstufe 2)

---

## 🔚 Zusammenfassung
> Mit einem einfachen BLE-Protokoll und WebBluetooth kann der Browser direkt kleine JPEG-Bilder (240×240 px) an den ESP32 übertragen.  
> Damit entsteht eine **kabellose, plattformunabhängige Pipeline**:  
> Web-Tool → BLE → ESP32 → Display.  
> Ideal für die LCD-Brosche-Projekte.
