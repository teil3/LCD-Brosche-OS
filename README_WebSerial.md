# 🔌 Erweiterung: Bildübertragung via USB (Web Serial API) zum ESP32

## 🧠 Ziel
Ergänzend zur bestehenden Bluetooth-LE-Funktion soll der `bildaufbereiter`-Web-Converter auch **Bilder über USB** an den ESP32 übertragen können.  
Damit entsteht eine zweite, robustere Verbindungsmöglichkeit für Systeme, auf denen Bluetooth nicht verfügbar oder unzuverlässig ist (z. B. Linux, macOS).

Ziel ist:
> Bilder (240×240 JPEGs) direkt aus dem Browser über die **Web Serial API** an den ESP32 senden.

---

## ⚙️ Grundprinzip

Die **Web Serial API** erlaubt es, im Browser über eine serielle Schnittstelle mit Geräten wie dem ESP32 zu kommunizieren – ganz ohne zusätzliche Software.

1. Der Benutzer klickt in der Web-App auf „Über USB senden“.
2. Der Browser fragt nach der Freigabe eines seriellen Ports.
3. Das Web-Tool öffnet den Port mit z. B. **921600 Baud**.
4. Das JPEG wird in Blöcken (z. B. 1024 Byte) übertragen.
5. Der ESP32 empfängt, speichert und zeigt das Bild an.

---

## 🌐 Browser- & OS-Kompatibilität

| Plattform | Browser | Unterstützung | Hinweise |
|------------|----------|----------------|-----------|
| **Windows 10+** | Chrome / Edge | ✅ stabil | COM-Port automatisch erkannt |
| **macOS** | Chrome / Edge | ✅ stabil | Gerät als `/dev/cu.*` verfügbar |
| **Linux** | Chrome / Edge / Brave / Opera | ✅ stabil | Benutzer muss in `dialout`-Gruppe sein |
| **ChromeOS** | Chrome | ✅ |
| **Android / iOS** | – | ❌ (nicht unterstützt) |
| **Firefox / Safari (Desktop)** | – | ❌ (noch kein Support) |

> Web Serial ist heute die **stabilste plattformübergreifende USB-API für den Browser**,  
> läuft auf allen Desktop-Systemen mit Chromium-basierten Browsern.  
> HTTPS oder `localhost` sind Voraussetzung.

---

## 🔧 Technische Umsetzung

### 1. UI-Erweiterung im `bildaufbereiter`
Ein zusätzlicher Button neben dem Bluetooth-Sende-Button:

```html
<button id="usbSendBtn">🔌 Über USB senden</button>
```

Dieser ruft per JavaScript eine Funktion auf, die die Web Serial API nutzt.

---

### 2. Browser-Seite (JavaScript)
**Aufgabe:** Verbindung öffnen, Header senden, Bild in Blöcken übertragen.

```js
async function sendImageOverSerial(imageUint8Array) {
  // Benutzer muss aktiv den Port auswählen
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 921600 });

  const writer = port.writable.getWriter();
  const encoder = new TextEncoder();

  // 1. Header mit Dateigrösse senden
  await writer.write(encoder.encode(`START ${imageUint8Array.length}\n`));

  // 2. Bilddaten blockweise übertragen
  const CHUNK_SIZE = 1024;
  for (let i = 0; i < imageUint8Array.length; i += CHUNK_SIZE) {
    const chunk = imageUint8Array.slice(i, i + CHUNK_SIZE);
    await writer.write(chunk);
  }

  // 3. Endsignal senden
  await writer.write(encoder.encode("END\n"));

  writer.releaseLock();
  await port.close();
  alert("Übertragung abgeschlossen ✅");
}
```

**Einbindung in die Web-App:**
- Der vorhandene JPEG-Encoder liefert ein `Uint8Array` (z. B. `jpegU8`).
- Der Button-Handler ruft `sendImageOverSerial(jpegU8)` auf.

---

### 3. ESP32-Seite (Arduino-Code)
**Aufgabe:** Empfängt die Datei über `Serial`, speichert sie z. B. in SPIFFS oder SD und zeigt sie optional an.

```cpp
#include <FS.h>
#include <SPIFFS.h>

void setup() {
  Serial.begin(921600);
  SPIFFS.begin(true);
  Serial.println("Ready for image...");
}

void loop() {
  static File img;
  static size_t remaining = 0;

  if (Serial.available()) {
    if (!remaining) {
      // Header lesen: "START <size>\n"
      String line = Serial.readStringUntil('\n');
      if (line.startsWith("START ")) {
        remaining = line.substring(6).toInt();
        img = SPIFFS.open("/img.jpg", FILE_WRITE);
        Serial.println("Receiving...");
      }
    } else {
      while (remaining && Serial.available()) {
        uint8_t buf[512];
        size_t n = Serial.readBytes(buf, min(sizeof(buf), remaining));
        img.write(buf, n);
        remaining -= n;
        if (!remaining) {
          img.close();
          Serial.println("DONE");
          // Optional: sofort anzeigen
          // TJpg_Decoder.drawFsJpg(0, 0, "/img.jpg");
        }
      }
    }
  }
}
```

---

## 🚀 Vorteile der Web Serial Methode

- **Zuverlässig und schnell** (meist schneller als BLE)
- **Kein Pairing** oder spezielle Treiber nötig (standardmäßiger USB-UART genügt)
- **Einfach zu integrieren** – nur wenige Zeilen JavaScript + Serial-Code
- **Offline-fähig** (funktioniert auch mit `localhost`)
- **Sicher** – Browser fordert immer aktive Gerätefreigabe an

---

## ⚠️ Einschränkungen
- Funktioniert **nicht in Firefox oder Safari**
- Nur **Desktop**-Browser (kein Android/iOS)
- Unter Linux müssen Portrechte korrekt gesetzt sein:
  ```bash
  sudo usermod -a -G dialout $USER
  ```
- Nur für den Browserstart nach Benutzerinteraktion erlaubt  
  (`navigator.serial.requestPort()` darf **nicht automatisch** aufgerufen werden)

---

## 🔚 Zusammenfassung
> Mit der **Web Serial API** kann der `bildaufbereiter` Bilder direkt über USB an den ESP32 senden – schnell, stabil und ohne zusätzliche Software.  
> Diese Methode ergänzt die bestehende Bluetooth-LE-Übertragung perfekt und sorgt für breite Kompatibilität auf allen Desktop-Systemen.

---

## 🧱 Nächste Schritte für Codex
1. Web-UI um USB-Senden-Button erweitern.  
2. Funktion `sendImageOverSerial()` implementieren (siehe Beispiel).  
3. Bestehendes JPEG-Encoding-Ergebnis (`Uint8Array`) als Eingabe verwenden.  
4. ESP32-Serial-Protokoll wie oben beschrieben implementieren.  
5. Test auf Linux und macOS mit Chrome/Edge.
