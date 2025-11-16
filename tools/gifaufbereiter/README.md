# GIF-Aufbereiter für die LCD-Brosche

Browser-Tool, um animierte GIFs für den ESP32-BoardOS-Slideshow-Stack vorzubereiten:

- akzeptiert **JPG/PNG, GIF und kurze Videos (MP4/WebM)**
- skaliert auf die Zielauflösung (max. 240×240)
- reduziert die Farbanzahl (Median-Cut + optionales Floyd-Steinberg-Dithering)
- encodiert offline zu GIF89a und prüft, ob das **20&nbsp;kB Upload-Limit** eingehalten wird

## Nutzung

1. `index.html` lokal öffnen (Doppelklick) oder als statische Seite hosten (z.&nbsp;B. GitHub Pages).
2. Datei per Drag&Drop oder Dateiauswahl laden.
3. Zielgröße, Farbanzahl, FPS (bei Video) und max. Frames einstellen.
4. **„GIF erzeugen“** klicken – nach erfolgreicher Optimierung steht der Download bereit, sofern die Ausgabedatei unter dem gewählten Limit bleibt.

> Für GIF-Parsing wird der **ImageDecoder** aus den WebCodecs benötigt. Das Tool ist mit Chromium-basierten Browsern (Chrome, Edge) getestet. Ohne ImageDecoder können zwar Bilder/Videos geladen, aber keine GIFs importiert werden.

## Dateien

- `index.html` – UI/Layout (keine externen Abhängigkeiten)
- `main.js` – Dateihandling, Video/GIF-Dekodierung, UI-Logik
- `gif-encoder.js` – Minimaler GIF89a-Encoder (LZW + lokale Farbtabellen)
- `quantize.js` – Median-Cut-Quantisierung inkl. optionalem Floyd-Steinberg-Dithering

## Tipps

- Für sehr kurze Clips lieber FPS reduzieren (z.&nbsp;B. 6–8 fps) statt viele Frames zu behalten.
- GIFs mit großem transparentem Anteil profitieren vom Dithering – falls Artefakte auftreten, Dithering deaktivieren und Farbanzahl erhöhen.
- Wird das 20&nbsp;kB-Limit überschritten, blendet das Tool einen Hinweis ein und deaktiviert den Download-Button.
