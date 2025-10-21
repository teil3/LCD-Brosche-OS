# 240×240 Baseline‑JPEG (MozJPEG/WASM, Browser‑only)

Dieses kleine Tool läuft komplett **im Browser**: Bild laden → auf **240×240** skalieren → mit **MozJPEG** als **Baseline (nicht progressiv)** speichern. Ideal für Geräte/Decoder, die *progressive JPEG* nicht unterstützen (z. B. manche Arduino‑Setups).

## Starten

1. Öffne einfach `index.html` im Browser (Doppelklick) **oder** hoste den Ordner z. B. mit GitHub Pages / einem statischen Webserver.
2. Bild auswählen → ggf. Zuschnitt/Qualität/Subsampling wählen → **„Als JPEG speichern“**.

> Standardmäßig lädt die Seite den MozJPEG‑WASM‑Codec von **UNPKG (@jsquash/jpeg@1.6.0)**. Die Verarbeitung bleibt lokal; es wird nichts hochgeladen.

## 100 % nicht progressiv (Baseline)

Wir setzen **`baseline: true`** und **`progressive: false`** in den MozJPEG‑Optionen. Nach dem Encoden prüft das Skript zusätzlich den JPEG‑Header (SOF‑Marker), um sicherzugehen, dass **kein SOF2 (progressiv)** enthalten ist.

## Offline / ohne CDN (optional)

Wenn du komplett offline sein willst:

1. Lade aus `https://unpkg.com/@jsquash/jpeg@1.6.0/codec/enc/` die beiden Dateien
   - `mozjpeg_enc.js`
   - `mozjpeg_enc.wasm`
2. Lege sie in **`./mozjpeg/`** in diesem Projekt ab.
3. Öffne `index.html` und ersetze die Import‑Zeile

```js
const moduleFactory = (await import('https://unpkg.com/@jsquash/jpeg@1.6.0/codec/enc/mozjpeg_enc.js?module')).default;
```

durch

```js
const moduleFactory = (await import('./mozjpeg/mozjpeg_enc.js')).default;
```

> Wichtig: `mozjpeg_enc.wasm` **muss im selben Ordner** wie `mozjpeg_enc.js` liegen, damit das automatische Nachladen funktioniert.

## Subsampling‑Werte

- `0` → **4:4:4** (beste Farbauflösung, größere Dateien)  
- `1` → **4:2:2**  
- `2` → **4:2:0** (kleinere Dateien, Standard)

## Deployment (z. B. GitHub Pages)

- Repo erstellen, Dateien hochladen, **Pages** auf Branch `main` + Verzeichnis `/` aktivieren.  
- Aufruf: `https://<USERNAME>.github.io/<REPO>/`

## Quellen

- jSquash (MozJPEG‑WASM für Browser) – https://github.com/jamsinclair/jSquash  
- MozJPEG Optionen (Typdefinitionen, inkl. `baseline`/`progressive`) – `@jsquash/jpeg@1.6.0/codec/enc/mozjpeg_enc.d.ts`

Viel Spaß!
