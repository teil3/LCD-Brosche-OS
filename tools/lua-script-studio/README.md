# Lua Script Studio

Webbasierter Editor für die Lua-App der LCD-Brosche. Die Seite funktioniert als reines Frontend (kein Backend nötig) und bietet:

- GitHub-Bibliothek: Lädt alle Skripte aus `assets/scripts/` (via GitHub API) und erlaubt das direkte Öffnen im Editor.
- Syntax-Highlighting & Autovervollständigung basierend auf CodeMirror (Lua-Modus).
- Live-Vorschau im Browser mit einem kleinen Lua-Runtime (Fengari). Unterstützt `brosche.clear/fill/rect/text/rgb/log` und simuliert das 240×240-Display.
- Datei-Workflow wie beim Bildaufbereiter/TextApp-Konfigurator: Skript herunterladen, per WebSerial (USB) oder WebBluetooth (BLE) auf die Brosche hochladen.
- Upload-Ziel ist immer `/scripts/<name>.lua`. Das Tool erzeugt das Verzeichnis bei Bedarf automatisch (USB: via Kommando; BLE: Gerät schreibt jetzt `.lua` nach `/scripts`).
- Drag&Drop für lokale `.lua`-Dateien sowie Autospeicher im `localStorage`.

## Starten

Öffne `index.html` direkt im Browser (Doppelklick) oder hoste den Ordner über einen einfachen HTTP-Server (z. B. `python3 -m http.server`).

## Abhängigkeiten (CDN)

- [CodeMirror 5.65.16](https://codemirror.net/) – Syntax-Highlighting
- [Fengari Web 0.1.4](https://fengari.io/) – Lua-VM in WebAssembly

> Falls du komplett offline arbeiten willst, kannst du die CDNs durch lokale Dateien ersetzen (ähnlich wie beim Bildaufbereiter).

## Upload-Flows

1. **USB**: Chrome/Edge auf dem Desktop, WebSerial aktiviert. In der Slideshow BTN2 lang halten (Transfer-Modus), dann "Über USB senden" klicken und den passenden Seriell-Port wählen.
2. **BLE**: Chrome/Edge (Desktop oder Android) mit WebBluetooth. Ebenfalls Transfer-Modus aktivieren, dann "Über BLE senden" und die Brosche aus der Geräteliste wählen.

Beide Wege senden eine einzelne `.lua`-Datei und legen sie direkt unter `/scripts` an, damit die Lua-App sie sofort findet.

## Vorschau

Die Vorschau ruft automatisch `setup()` und danach `loop(dt)` mit einer Bildwiederholrate per `requestAnimationFrame` auf. `brosche.log()` und `print()` landen im Log-Fenster unterhalb der Vorschau. Farben werden wie auf dem Gerät als 16-Bit-RGB565 interpretiert.

Viel Spaß! ✨
