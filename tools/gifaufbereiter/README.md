# GIF-Aufbereiter für die LCD-Brosche

Browser-Tool, um animierte GIFs für den ESP32-BoardOS-Slideshow-Stack vorzubereiten:

- akzeptiert **JPG/PNG, GIF und kurze Videos (MP4/WebM)**
- skaliert auf die Zielauflösung (max. 240×240)
- reduziert die Farbanzahl (Median-Cut + optionales Floyd-Steinberg-Dithering)
- encodiert offline zu GIF89a und prüft, ob das **20&nbsp;kB Upload-Limit** eingehalten wird

## Nutzung

1. Seite über einen lokalen Webserver starten (z.&nbsp;B. `cd tools && python3 -m http.server 8080` → `http://localhost:8080/gifaufbereiter/`) oder per GitHub Pages hosten. **Direktes Öffnen via `file://` funktioniert nicht**, da Browser keine ES-Module von Origin `null` laden.
2. Datei per Drag&Drop oder Dateiauswahl laden.
3. Zielgröße, Farbanzahl, FPS (bei Video) und max. Frames einstellen.
4. **„GIF erzeugen“** klicken – nach erfolgreicher Optimierung steht der Download bereit, sofern die Ausgabedatei unter dem gewählten Limit bleibt.

> Für GIF-Parsing wird der **ImageDecoder** aus den WebCodecs benötigt. Das Tool ist mit Chromium-basierten Browsern (Chrome, Edge) getestet. Ohne ImageDecoder können zwar Bilder/Videos geladen, aber keine GIFs importiert werden.
>
> Hintergrund zum 20 kB-Limit: Im aktuellen USB-/BLE-Workflow stehen ~37 kB RAM zur Verfügung, davon nur 20 kB als zusammenhängender Block fürs Hochladen. Deshalb erzwingt der GIF-Aufbereiter das Limit und blockiert Downloads oberhalb dieser Grenze.

## Dateien

- `index.html` – UI/Layout (keine externen Abhängigkeiten)
- `main.js` – Dateihandling, Video/GIF-Dekodierung, UI-Logik
- `gif-encoder.js` – Minimaler GIF89a-Encoder (LZW + lokale Farbtabellen)
- `quantize.js` – Median-Cut-Quantisierung inkl. optionalem Floyd-Steinberg-Dithering

## Aktueller Stand (November 2025)

- GIF-Dekodierung (über WebCodecs), Resampling, Farbreduktion, LZW-Encoder und 2×-Vorschau sind produktiv einsetzbar.
- Der Encoder erzeugt gültige GIF89a-Dateien; ein defekter LZW-Datenstrom aus frühen Builds wurde behoben.
- Downloads bleiben deaktiviert, solange das Limit überschritten wird; die Limit-Ampel wechselt zwischen Grün (OK) und Rot (zu groß).
- Nächste Schritte auf der Roadmap: Batch-Verarbeitung, direkter USB-/BLE-Transfer wie beim Bildaufbereiter sowie Presets für typische Broschen-Animationen.

## Tipps

- Für sehr kurze Clips lieber FPS reduzieren (z.&nbsp;B. 6–8 fps) statt viele Frames zu behalten.
- GIFs mit großem transparentem Anteil profitieren vom Dithering – falls Artefakte auftreten, Dithering deaktivieren und Farbanzahl erhöhen.
- Wird das 20&nbsp;kB-Limit überschritten, blendet das Tool einen Hinweis ein und deaktiviert den Download-Button.
